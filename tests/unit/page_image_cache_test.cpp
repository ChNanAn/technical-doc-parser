#include "document_intelligence_engine/document_engine.h"
#include "document_source/document_source_factory.h"
#include "export/json_document_exporter.h"
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
#include <limits>
#include <memory>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
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

    doc_parser::document::PageArtifact pageArtifact() const {
        doc_parser::document::PageArtifact page;
        page.page_index = 2;
        page.page_number = 3;
        page.output_path = root / "rendered.png";
        page.width = 8;
        page.height = 8;
        return page;
    }

    doc_parser::document::PageBitmap rgbaBitmap(const doc_parser::document::PageArtifact& page) const {
        doc_parser::document::PageBitmap bitmap;
        bitmap.page_index = page.page_index;
        bitmap.page_number = page.page_number;
        bitmap.width = page.width;
        bitmap.height = page.height;
        bitmap.pixels.resize(static_cast<std::size_t>(page.width) * page.height * 4);
        for (std::size_t i = 0; i < bitmap.pixels.size(); i += 4) {
            const auto value = static_cast<unsigned char>(i);
            bitmap.pixels[i] = value;
            bitmap.pixels[i + 1] = i % 8 == 0 ? value : 93;
            bitmap.pixels[i + 2] = i % 8 == 0 ? value : 201;
            bitmap.pixels[i + 3] = value;
        }
        return bitmap;
    }

    std::filesystem::path root;
};

TEST_F(PageImageCacheTest, RenderedPixelsConvertLazilyAndMatchPngIncludingGrayAndAlpha) {
    const auto page = pageArtifact();
    auto bitmap = rgbaBitmap(page);
    const cv::Mat rgba(page.height, page.width, CV_8UC4, bitmap.pixels.data());
    cv::Mat bgra;
    cv::cvtColor(rgba, bgra, cv::COLOR_RGBA2BGRA);
    ASSERT_TRUE(cv::imwrite(page.output_path.string(), bgra));
    const auto expected = cv::imread(page.output_path.string(), cv::IMREAD_COLOR);
    const auto retained = bitmap.pixels.capacity();
    PageImageCache cache(retained);
    ASSERT_TRUE(cache.admitRendered(page, std::move(bitmap)));
    EXPECT_TRUE(bitmap.pixels.empty());
    EXPECT_EQ(cache.stats().conversions, 0U);
    EXPECT_EQ(cache.stats().resident_bytes, retained);
    // The handoff is sufficient even if the PNG is no longer available.
    ASSERT_TRUE(std::filesystem::remove(page.output_path));
    const auto first = cache.read(page.output_path);
    ASSERT_FALSE(first.empty());
    EXPECT_EQ(cv::norm(first, expected, cv::NORM_INF), 0);
    EXPECT_EQ(first.at<cv::Vec3b>(0, 0), cv::Vec3b(0, 0, 0));
    EXPECT_EQ(first.at<cv::Vec3b>(0, 1), cv::Vec3b(201, 93, 4));
    EXPECT_EQ(cache.read(page.output_path).data, first.data);
    EXPECT_EQ(cache.stats().conversions, 1U);
    EXPECT_EQ(cache.stats().decodes, 0U);
    EXPECT_EQ(cache.stats().hits, 2U);
    EXPECT_EQ(cache.stats().resident_bytes, 8U * 8 * 3);
    EXPECT_EQ(cache.stats().peak_resident_bytes, retained);
    cache.clear();
    EXPECT_EQ(cache.stats().resident_bytes, 0U);
    EXPECT_EQ(cv::norm(first, expected, cv::NORM_INF), 0);
}

TEST_F(PageImageCacheTest, UnusedRenderedPixelsAreReleasedWithoutConversion) {
    const auto page = pageArtifact();
    PageImageCache cache(4096);
    ASSERT_TRUE(cache.admitRendered(page, rgbaBitmap(page)));
    cache.clear();
    EXPECT_EQ(cache.stats().resident_bytes, 0U);
    EXPECT_EQ(cache.stats().conversions, 0U);
    EXPECT_EQ(cache.stats().decodes, 0U);
    auto next = rgbaBitmap(page);
    next.pixels[0] = 99;
    ASSERT_TRUE(cache.admitRendered(page, std::move(next)));
    EXPECT_EQ(cache.read(page.output_path).at<cv::Vec3b>(0, 0), cv::Vec3b(0, 0, 99));
}

TEST_F(PageImageCacheTest, RenderAdmissionHonorsCapacityRemainingBudgetAndFileFallback) {
    const auto page = pageArtifact();
    ASSERT_TRUE(cv::imwrite(page.output_path.string(), cv::Mat(8, 8, CV_8UC3, cv::Scalar(1, 2, 3))));
    for (const std::size_t budget : {0U, 8U * 8 * 3, 8U * 8 * 4 - 1}) {
        PageImageCache cache(budget);
        auto bitmap = rgbaBitmap(page);
        const auto* pixels = bitmap.pixels.data();
        EXPECT_FALSE(cache.admitRendered(page, std::move(bitmap)));
        EXPECT_EQ(bitmap.pixels.data(), pixels);
        EXPECT_EQ(cache.stats().resident_bytes, 0U);
        EXPECT_EQ(cache.read(page.output_path).at<cv::Vec3b>(0, 0), cv::Vec3b(1, 2, 3));
        EXPECT_FALSE(cache.read(page.output_path).empty());
        EXPECT_EQ(cache.stats().decodes, budget == 0 ? 2U : 1U);
        EXPECT_LE(cache.stats().peak_resident_bytes, budget);
    }
    auto reserved = rgbaBitmap(page);
    reserved.pixels.reserve(4096);
    PageImageCache cache(512);
    EXPECT_FALSE(cache.admitRendered(page, std::move(reserved)));
    ASSERT_TRUE(cache.admitRendered(page, rgbaBitmap(page)));
    EXPECT_FALSE(cache.admitRendered(page, rgbaBitmap(page)));
    auto second = page;
    second.output_path = root / "second.png";
    ASSERT_TRUE(cache.admitRendered(second, rgbaBitmap(second)));
    auto third = page;
    third.output_path = root / "third.png";
    EXPECT_FALSE(cache.admitRendered(third, rgbaBitmap(third)));
    EXPECT_EQ(cache.stats().resident_bytes, 512U);
}

TEST_F(PageImageCacheTest, InvalidRenderedBuffersNeverEnterTheCache) {
    const auto page = pageArtifact();
    PageImageCache cache(std::numeric_limits<std::size_t>::max());
    for (int defect = 0; defect < 7; ++defect) {
        auto artifact = page;
        auto bitmap = rgbaBitmap(page);
        switch (defect) {
        case 0:
            bitmap.width = 0;
            break;
        case 1:
            bitmap.channels = 3;
            break;
        case 2:
            bitmap.pixels.pop_back();
            break;
        case 3:
            ++bitmap.page_index;
            break;
        case 4:
            ++bitmap.page_number;
            break;
        case 5:
            ++bitmap.height;
            break;
        case 6:
            artifact.width = bitmap.width = std::numeric_limits<int>::max();
            artifact.height = bitmap.height = std::numeric_limits<int>::max();
            break;
        }
        EXPECT_FALSE(cache.admitRendered(artifact, std::move(bitmap))) << defect;
    }
    EXPECT_EQ(cache.stats().resident_bytes, 0U);
    EXPECT_EQ(cache.stats().rendered_rejections, 7U);
    ASSERT_TRUE(cache.admitRendered(page, rgbaBitmap(page)));
    EXPECT_FALSE(cache.read(page.output_path).empty());
}

TEST_F(PageImageCacheTest, ConcurrentReadersShareOneRenderedConversion) {
    const auto page = pageArtifact();
    PageImageCache cache(4096);
    ASSERT_TRUE(cache.admitRendered(page, rgbaBitmap(page)));
    std::vector<std::future<cv::Mat>> readers;
    for (int i = 0; i < 8; ++i) {
        readers.push_back(std::async(std::launch::async, [&] { return cache.read(page.output_path); }));
    }
    const auto first = readers.front().get();
    ASSERT_FALSE(first.empty());
    for (std::size_t i = 1; i < readers.size(); ++i) {
        EXPECT_EQ(readers[i].get().data, first.data);
    }
    EXPECT_EQ(cache.stats().conversions, 1U);
    EXPECT_EQ(cache.stats().decodes, 0U);
    EXPECT_EQ(cache.stats().hits, 8U);
}

TEST_F(PageImageCacheTest, PdfRendererHandsOffPixelsOnlyAfterSuccessfulPngPublication) {
    auto source = doc_parser::document_source::createDocumentSource("pdf");
    if (!source.source) {
        GTEST_SKIP() << "PDFium is disabled";
    }
    ASSERT_TRUE(source.source->open(std::filesystem::path(DOC_PARSER_TEST_FIXTURE_DIR) / "pdfs/pdfjs-basicapi.pdf"));
    PageImageCache cache(64 * 1024 * 1024);
    doc_parser::document_source::RenderRequest request{72, root, root / "pages"};
    int handoffs = 0;
    request.on_page_rendered = [&](const auto& page, auto&& bitmap) {
        ++handoffs;
        const auto png = cv::imread(page.output_path.string(), cv::IMREAD_COLOR);
        ASSERT_FALSE(png.empty());
        ASSERT_TRUE(cache.admitRendered(page, std::move(bitmap)));
        EXPECT_EQ(cv::norm(png, cache.read(page.output_path), cv::NORM_INF), 0);
    };
    doc_parser::document::PageArtifact page;
    ASSERT_TRUE(source.renderer->renderPage(request, 0, page));
    EXPECT_EQ(handoffs, 1);
    EXPECT_FALSE(source.renderer->renderPage(request, source.source->pageCount(), page));
    EXPECT_EQ(handoffs, 1);
    ASSERT_TRUE(std::filesystem::create_directory(root / "pages/page_2.png"));
    EXPECT_FALSE(source.renderer->renderPage(request, 1, page));
    EXPECT_EQ(handoffs, 1);
}

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
    bool read_images = true;
};

class ImageReadingLayout final : public doc_parser::layout::ILayoutBackend {
public:
    explicit ImageReadingLayout(std::shared_ptr<PipelineImageProbe> probe) : probe_(std::move(probe)) {}
    bool analyze(const doc_parser::layout::LayoutRequest& request,
                 doc_parser::layout::LayoutResult& result) const override {
        probe_->cache = request.page.image_cache;
        if (probe_->read_images) {
            const auto pixels = readPageImage(request.page);
            if (pixels.empty()) {
                return false;
            }
            EXPECT_EQ(cv::norm(pixels, cv::imread(request.page.output_path.string(), cv::IMREAD_COLOR), cv::NORM_INF),
                      0);
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
        if (probe_->read_images && readPageImage(request.page).empty()) {
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
    EXPECT_EQ(probe->last_table_stats.decodes, 0U);
    EXPECT_EQ(probe->last_table_stats.conversions, 3U);
    EXPECT_EQ(probe->last_table_stats.hits, 6U);
    EXPECT_TRUE(probe->cache.expired());

    const auto& image = first.artifacts.pages.front().image;
    const auto page_bytes = static_cast<std::size_t>(image.width) * image.height * 3;
    EXPECT_EQ(probe->last_table_stats.peak_resident_bytes, page_bytes / 3 * 4);
    options.image_cache_bytes = page_bytes;
    const auto fallback = engine.parse(options);
    ASSERT_TRUE(fallback.ok());
    EXPECT_EQ(probe->last_table_stats.decodes, 3U);
    EXPECT_EQ(probe->last_table_stats.conversions, 0U);
    EXPECT_EQ(probe->last_table_stats.hits, 3U);
    EXPECT_EQ(probe->last_table_stats.peak_resident_bytes, page_bytes);
    const doc_parser::exporter::JsonDocumentExporter exporter;
    const auto direct_json = exporter.serialize({true, &first.document, &first.artifacts});
    const auto fallback_json = exporter.serialize({true, &fallback.document, &fallback.artifacts});
    ASSERT_TRUE(direct_json.status.okStatus());
    ASSERT_TRUE(fallback_json.status.okStatus());
    EXPECT_EQ(direct_json.json, fallback_json.json);
    options.image_cache_bytes = page_bytes / 3 * 4;
    ASSERT_TRUE(engine.parse(options).ok());
    EXPECT_EQ(probe->last_table_stats.decodes, 0U);
    EXPECT_EQ(probe->last_table_stats.conversions, 3U);
    options.image_cache_bytes = 64 * 1024 * 1024;

    // Reuse the output paths while retaining the earlier ParseResult.
    options.render.dpi = 100;
    const auto second = engine.parse(options);
    ASSERT_TRUE(second.ok()) << second.status.message();
    EXPECT_EQ(probe->last_table_stats.decodes, 0U);
    EXPECT_EQ(probe->last_table_stats.conversions, 3U);
    EXPECT_EQ(probe->last_table_stats.hits, 6U);
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

    probe->read_images = false;
    options.image_cache_bytes = 64 * 1024 * 1024;
    ASSERT_TRUE(engine.parse(options).ok());
    EXPECT_EQ(probe->last_table_stats.rendered_admissions, 3U);
    EXPECT_EQ(probe->last_table_stats.conversions, 0U);
    EXPECT_EQ(probe->last_table_stats.decodes, 0U);
    EXPECT_TRUE(probe->cache.expired());
}

} // namespace
