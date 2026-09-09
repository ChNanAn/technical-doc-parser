#include "document_intelligence_engine/document_engine.h"
#include "document_source/document_source_factory.h"
#include "image/image_preprocessor.h"
#include "image/page_image_cache.h"
#include "layout/layout_backend.h"
#include "pipeline/backend_registry.h"
#include "pipeline/document_engine_internal.h"
#include "table/table_backend.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <opencv2/imgcodecs.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using doc_parser::image::PageImageCache;
using doc_parser::image::readPageImage;

class PageImageCacheTest : public testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               ("tdp_images_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(root));
    }
    void TearDown() override { std::filesystem::remove_all(root); }

    std::filesystem::path root;
};

TEST_F(PageImageCacheTest, PreservesColorDecodingForGrayscaleBgrAndAlphaInputs) {
    PageImageCache cache(4096);
    for (const int channels : {1, 3, 4}) {
        const auto path = root / (std::to_string(channels) + ".png");
        const cv::Mat input(8, 8, CV_MAKETYPE(CV_8U, channels), cv::Scalar(17, 93, 201, 41));
        ASSERT_TRUE(cv::imwrite(path.string(), input));
        const cv::Mat expected = cv::imread(path.string(), cv::IMREAD_COLOR);
        const cv::Mat first = cache.read(path);
        const cv::Mat second = cache.read(path);
        ASSERT_EQ(first.type(), CV_8UC3);
        EXPECT_EQ(cv::norm(first, expected, cv::NORM_INF), 0);
        EXPECT_EQ(first.data, second.data);
        EXPECT_EQ(first.at<cv::Vec3b>(0, 0), channels == 1 ? cv::Vec3b(17, 17, 17) : cv::Vec3b(17, 93, 201));
    }
    EXPECT_EQ(cache.stats().decodes, 3U);
    EXPECT_EQ(cache.stats().hits, 3U);
}

TEST_F(PageImageCacheTest, LongStageScansKeepAdmittedPagesWithinTheByteBudget) {
    constexpr std::size_t page_bytes = 8 * 8 * 3;
    PageImageCache cache(2 * page_bytes);
    std::vector<std::filesystem::path> paths;
    for (int i = 0; i < 6; ++i) {
        paths.push_back(root / (std::to_string(i) + ".png"));
        ASSERT_TRUE(cv::imwrite(paths.back().string(), cv::Mat(8, 8, CV_8UC3, cv::Scalar(i, 0, 0))));
    }
    for (int stage = 0; stage < 3; ++stage) {
        for (std::size_t i = 0; i < paths.size(); ++i) {
            const cv::Mat image = cache.read(paths[i]);
            ASSERT_FALSE(image.empty());
            EXPECT_EQ(image.at<cv::Vec3b>(0, 0)[0], i);
            EXPECT_LE(cache.stats().resident_bytes, 2 * page_bytes);
        }
    }
    EXPECT_EQ(cache.stats().hits, 4U);
    EXPECT_EQ(cache.stats().decodes, 14U);
    EXPECT_EQ(cache.stats().uncached_decodes, 12U);
}

TEST_F(PageImageCacheTest, OversizedImageDoesNotPreventLaterSmallImageAdmission) {
    const auto large = root / "large.png";
    const auto small = root / "small.png";
    ASSERT_TRUE(cv::imwrite(large.string(), cv::Mat(20, 20, CV_8UC3, cv::Scalar(1, 2, 3))));
    ASSERT_TRUE(cv::imwrite(small.string(), cv::Mat(8, 8, CV_8UC3, cv::Scalar(3, 2, 1))));
    PageImageCache cache(8 * 8 * 3);
    ASSERT_FALSE(cache.read(large).empty());
    EXPECT_EQ(cache.stats().resident_bytes, 0U);
    ASSERT_FALSE(cache.read(small).empty());
    ASSERT_FALSE(cache.read(small).empty());
    EXPECT_EQ(cache.stats().hits, 1U);
    EXPECT_EQ(cache.stats().resident_bytes, 8U * 8 * 3);
}

TEST_F(PageImageCacheTest, ZeroBudgetDisablesRetention) {
    const auto path = root / "page.png";
    ASSERT_TRUE(cv::imwrite(path.string(), cv::Mat(8, 8, CV_8UC3, cv::Scalar(1, 2, 3))));
    PageImageCache cache(0);
    ASSERT_FALSE(cache.read(path).empty());
    ASSERT_FALSE(cache.read(path).empty());
    EXPECT_EQ(cache.stats().resident_bytes, 0U);
    EXPECT_EQ(cache.stats().decodes, 2U);
    EXPECT_EQ(cache.stats().hits, 0U);
}

TEST_F(PageImageCacheTest, FailedDecodeIsNotRetained) {
    const auto path = root / "page.png";
    PageImageCache cache(4096);
    EXPECT_TRUE(cache.read(path).empty());
    ASSERT_TRUE(cv::imwrite(path.string(), cv::Mat(8, 8, CV_8UC3, cv::Scalar(1, 2, 3))));
    EXPECT_FALSE(cache.read(path).empty());
    EXPECT_EQ(cache.stats().failures, 1U);
    EXPECT_EQ(cache.stats().decodes, 1U);
}

TEST_F(PageImageCacheTest, ArtifactsDoNotKeepRunPixelsAliveOrReuseThemInTheNextRun) {
    doc_parser::document::PageArtifact page;
    page.output_path = root / "page.png";
    ASSERT_TRUE(cv::imwrite(page.output_path.string(), cv::Mat(8, 8, CV_8UC3, cv::Scalar(1, 2, 3))));
    auto first_run = std::make_shared<PageImageCache>(4096);
    page.image_cache = first_run;
    EXPECT_EQ(readPageImage(page).at<cv::Vec3b>(0, 0), cv::Vec3b(1, 2, 3));
    const auto artifact = page;
    first_run.reset();
    EXPECT_TRUE(artifact.image_cache.expired());

    ASSERT_TRUE(cv::imwrite(page.output_path.string(), cv::Mat(8, 8, CV_8UC3, cv::Scalar(7, 8, 9))));
    EXPECT_EQ(readPageImage(artifact).at<cv::Vec3b>(0, 0), cv::Vec3b(7, 8, 9));
    const auto next_run = std::make_shared<PageImageCache>(4096);
    page.image_cache = next_run;
    EXPECT_EQ(readPageImage(page).at<cv::Vec3b>(0, 0), cv::Vec3b(7, 8, 9));
    EXPECT_EQ(next_run->stats().decodes, 1U);
}

TEST_F(PageImageCacheTest, ConcurrentReadersShareOneDecode) {
    const auto path = root / "page.png";
    ASSERT_TRUE(cv::imwrite(path.string(), cv::Mat(8, 8, CV_8UC3, cv::Scalar(1, 2, 3))));
    PageImageCache cache(4096);
    std::vector<std::future<cv::Mat>> readers;
    for (int i = 0; i < 8; ++i) {
        readers.push_back(std::async(std::launch::async, [&] { return cache.read(path); }));
    }
    const cv::Mat first = readers.front().get();
    ASSERT_FALSE(first.empty());
    for (std::size_t i = 1; i < readers.size(); ++i) {
        EXPECT_EQ(readers[i].get().data, first.data);
    }
    EXPECT_EQ(cache.stats().decodes, 1U);
    EXPECT_EQ(cache.stats().hits, 7U);
}

TEST_F(PageImageCacheTest, DebugPreprocessingPreservesSharedPixelsAndFileOutput) {
    const auto path = root / "page.png";
    cv::Mat input(8, 8, CV_8UC3);
    cv::randu(input, cv::Scalar::all(0), cv::Scalar::all(255));
    ASSERT_TRUE(cv::imwrite(path.string(), input));
    PageImageCache cache(4096);
    const doc_parser::image::ImagePreprocessor preprocessor;
    ASSERT_TRUE(preprocessor.preprocessFile(path, root / "from_file.png"));
    ASSERT_TRUE(preprocessor.preprocessToFile(cache.read(path), root / "nested" / "from_cache.png"));
    EXPECT_EQ(cv::norm(input, cache.read(path), cv::NORM_INF), 0);
    EXPECT_EQ(cv::norm(cv::imread((root / "from_file.png").string()),
                       cv::imread((root / "nested" / "from_cache.png").string()),
                       cv::NORM_INF),
              0);
}

struct PipelineImageProbe {
    std::weak_ptr<PageImageCache> cache;
    doc_parser::image::PageImageCacheStats last_table_stats;
    bool fail_table = false;
};

class ImageReadingLayout final : public doc_parser::layout::ILayoutBackend {
public:
    explicit ImageReadingLayout(std::shared_ptr<PipelineImageProbe> probe) : probe_(std::move(probe)) {}
    bool analyze(const doc_parser::layout::LayoutRequest& request,
                 doc_parser::layout::LayoutResult& result) const override {
        probe_->cache = request.page.image_cache;
        if (readPageImage(request.page).empty()) {
            return false;
        }
        return doc_parser::layout::TextLayoutModelBackend().analyze(request, result);
    }

private:
    std::shared_ptr<PipelineImageProbe> probe_;
};

class ImageReadingTable final : public doc_parser::table::ITableBackend {
public:
    explicit ImageReadingTable(std::shared_ptr<PipelineImageProbe> probe) : probe_(std::move(probe)) {}
    bool recognize(const doc_parser::table::TableRequest& request,
                   doc_parser::table::TableResult& result) const override {
        if (readPageImage(request.page).empty()) {
            return false;
        }
        if (const auto cache = request.page.image_cache.lock()) {
            probe_->last_table_stats = cache->stats();
        }
        if (probe_->fail_table) {
            throw std::runtime_error("injected failure after reading page pixels");
        }
        return doc_parser::table::TextTableStructureBackend().recognize(request, result);
    }

private:
    std::shared_ptr<PipelineImageProbe> probe_;
};

TEST_F(PageImageCacheTest, PipelineSharesImagesAndReleasesThemAcrossSuccessFailureAndEngineReuse) {
    if (doc_parser::document_source::createDocumentSource("pdf").source == nullptr) {
        GTEST_SKIP() << "PDFium is disabled";
    }
    const auto probe = std::make_shared<PipelineImageProbe>();
    auto config = doc_parser::pipeline::defaultEngineConfig();
    auto registry = doc_parser::pipeline::createDefaultBackendRegistry(config);
    ASSERT_TRUE(
        registry.registerLayout("image-reader", [probe] { return std::make_unique<ImageReadingLayout>(probe); }));
    ASSERT_TRUE(registry.registerTable("image-reader", [probe] { return std::make_unique<ImageReadingTable>(probe); }));
    config.backends.document = "pdf";
    config.backends.ocr = "noop";
    config.backends.layout = "image-reader";
    config.backends.table = "image-reader";
    auto engine = doc_parser::pipeline::DocumentEngineInternalAccess::create(config, registry);
    ASSERT_TRUE(engine.isReady()) << engine.initializationStatus().message();
    doc_parser::pipeline::DocumentParseOptions options;
    options.input_path = std::filesystem::path(DOC_PARSER_TEST_FIXTURE_DIR) / "pdfs" / "pdfjs-basicapi.pdf";
    options.output_directory = root;
    options.render.dpi = 72;
    const auto first = engine.parse(options);
    ASSERT_TRUE(first.ok()) << first.status.message();
    EXPECT_EQ(probe->last_table_stats.decodes, 3U);
    EXPECT_EQ(probe->last_table_stats.hits, 3U);
    EXPECT_TRUE(probe->cache.expired());

    const auto& image = first.artifacts.pages.front().image;
    const auto page_bytes = static_cast<std::size_t>(image.width) * image.height * 3;
    EXPECT_EQ(probe->last_table_stats.peak_resident_bytes, page_bytes);
    options.image_cache_bytes = page_bytes;
    ASSERT_TRUE(engine.parse(options).ok());
    EXPECT_EQ(probe->last_table_stats.decodes, 3U);
    EXPECT_EQ(probe->last_table_stats.hits, 3U);
    EXPECT_EQ(probe->last_table_stats.peak_resident_bytes, page_bytes);
    options.image_cache_bytes = 64 * 1024 * 1024;

    // Reuse the output paths while retaining the earlier ParseResult.
    options.render.dpi = 100;
    const auto second = engine.parse(options);
    ASSERT_TRUE(second.ok()) << second.status.message();
    EXPECT_EQ(probe->last_table_stats.decodes, 3U);
    EXPECT_EQ(probe->last_table_stats.hits, 3U);
    EXPECT_TRUE(probe->cache.expired());

    probe->fail_table = true;
    EXPECT_FALSE(engine.parse(options).ok());
    EXPECT_TRUE(probe->cache.expired());
    probe->fail_table = false;
    options.image_cache_bytes = 0;
    ASSERT_TRUE(engine.parse(options).ok());
    EXPECT_EQ(probe->last_table_stats.decodes, 6U);
    EXPECT_EQ(probe->last_table_stats.hits, 0U);
    EXPECT_EQ(probe->last_table_stats.resident_bytes, 0U);
    EXPECT_TRUE(probe->cache.expired());
}

} // namespace
