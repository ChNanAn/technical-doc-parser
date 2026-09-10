#include "document/page_rotation.h"
#include "document_intelligence_engine/document_engine.h"
#include "image/page_image_cache.h"
#include "pipeline/backend_registry.h"
#include "pipeline/document_engine_internal.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {
namespace document = doc_parser::document;
namespace pipeline = doc_parser::pipeline;
namespace source = doc_parser::document_source;

cv::Mat uprightPixels() {
    cv::Mat pixels(60, 100, CV_8UC3, cv::Scalar(255, 255, 255));
    pixels(cv::Rect(10, 2, 20, 5)).setTo(cv::Scalar(10, 50, 90));
    return pixels;
}

class TwoPageSource final : public source::IDocumentSource, public source::IPageRenderer {
public:
    bool open(const std::filesystem::path& path) override {
        path_ = path;
        return true;
    }
    std::string sourcePath() const override { return path_.string(); }
    std::string sourceType() const override { return "png"; }
    int pageCount() const override { return 2; }
    bool supportsPageRendering() const override { return true; }
    bool renderPages(const source::RenderRequest&, std::vector<document::PageArtifact>&) const override {
        return false;
    }
    bool renderPage(const source::RenderRequest& request, int index, document::PageArtifact& page) const override {
        page = {};
        page.page_index = index;
        page.page_number = index + 1;
        page.width = 100;
        page.height = 60;
        page.relative_image = "pages/page_" + std::to_string(index + 1) + ".png";
        page.output_path = request.output_root / page.relative_image;
        std::filesystem::create_directories(request.pages_dir);
        auto pixels = uprightPixels();
        if (index == 1)
            cv::rotate(pixels, pixels, cv::ROTATE_180);
        return cv::imwrite(page.output_path.string(), pixels);
    }

private:
    std::filesystem::path path_;
};

document::BBox tableBox(int page) { return {10, page == 1 ? 50.0 : 1.0, 90, page == 1 ? 59.0 : 12.0}; }

class OrientedOcr final : public doc_parser::ocr::IOcrBackend {
public:
    bool recognize(const doc_parser::ocr::OcrRequest& request, doc_parser::ocr::OcrResult& result) const override {
        result = {};
        result.clockwise_correction_degrees = request.page.page_number == 2 ? 180 : 0;
        result.page_text.page_index = request.page.page_index;
        result.page_text.page_number = request.page.page_number;
        result.page_text.has_text = true;
        result.page_text.preferred_source = document::TextSource::Ocr;
        document::TextLine line;
        line.text = "table content";
        line.source = document::TextSource::Ocr;
        line.bbox = document::PageRotation(100, 60, result.clockwise_correction_degrees)
                        .toSource(tableBox(request.page.page_number));
        result.page_text.lines.push_back(line);
        return true;
    }
};

class FileLayout final : public doc_parser::layout::ILayoutBackend {
public:
    bool analyze(const doc_parser::layout::LayoutRequest& request,
                 doc_parser::layout::LayoutResult& result) const override {
        const auto pixels = cv::imread(request.page.output_path.string());
        if (pixels.empty() || cv::norm(pixels, uprightPixels(), cv::NORM_INF) != 0)
            return false;
        EXPECT_DOUBLE_EQ(request.text.lines[0].bbox.y0, tableBox(request.page.page_number).y0);
        result.layout.page_index = request.page.page_index;
        result.layout.page_number = request.page.page_number;
        document::LayoutBlock block;
        block.id = "table";
        block.type = document::LayoutBlockType::Table;
        block.bbox = tableBox(request.page.page_number);
        block.text_line_indices = {0};
        result.layout.blocks.push_back(block);
        return true;
    }
};

class CachedTable final : public doc_parser::table::ITableBackend {
public:
    bool recognize(const doc_parser::table::TableRequest& request,
                   doc_parser::table::TableResult& result) const override {
        const auto pixels = doc_parser::image::readPageImage(request.page);
        if (pixels.empty() || cv::norm(pixels, uprightPixels(), cv::NORM_INF) != 0)
            return false;
        result.tables.page_index = request.page.page_index;
        result.tables.page_number = request.page.page_number;
        document::Table table;
        table.id = "table_" + std::to_string(request.page.page_number);
        table.page_index = request.page.page_index;
        table.page_number = request.page.page_number;
        table.layout_block_id = "table";
        table.bbox = tableBox(request.page.page_number);
        table.columns.push_back({0, table.bbox, 1.0});
        document::TableRow row;
        row.bbox = table.bbox;
        document::TableCell cell;
        cell.bbox = table.bbox;
        cell.text = "table content";
        row.cells.push_back(cell);
        table.rows.push_back(row);
        result.tables.tables.push_back(table);
        return true;
    }
};

TEST(PipelineOrientationTest, MixedPagePosesShareImagesAndLinkTablesBeforeRestoringCoordinates) {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("tdp_mixed_orientation_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ASSERT_TRUE(std::filesystem::create_directory(root));
    // Own only this test-created directory, including when an assertion returns early.
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{root};
    const auto input = root / "source.png";
    ASSERT_TRUE(cv::imwrite(input.string(), uprightPixels()));
    auto config = pipeline::defaultEngineConfig();
    config.backends.document = config.backends.ocr = config.backends.layout = config.backends.table = "orientation-"
                                                                                                      "test";
    auto registry = pipeline::createDefaultBackendRegistry(config);
    registry.registerDocument("orientation-test", [] {
        auto backend = std::make_unique<TwoPageSource>();
        source::DocumentSourceBundle bundle;
        bundle.renderer = backend.get();
        bundle.source = std::move(backend);
        return bundle;
    });
    registry.registerOcr("orientation-test", [] { return std::make_unique<OrientedOcr>(); });
    registry.registerLayout("orientation-test", [] { return std::make_unique<FileLayout>(); });
    registry.registerTable("orientation-test", [] { return std::make_unique<CachedTable>(); });
    auto engine = pipeline::DocumentEngineInternalAccess::create(config, registry);
    pipeline::DocumentParseOptions options;
    options.input_path = input;
    options.output_directory = root / "output";
    for (const auto budget : {0U, 64000U}) {
        options.image_cache_bytes = budget;
        const auto result = engine.parse(options);
        ASSERT_TRUE(result.ok()) << result.status.message();
        ASSERT_EQ(result.artifacts.pages.size(), 2U);
        const auto& first = result.artifacts.pages[0].tables.tables.at(0);
        const auto& second = result.artifacts.pages[1].tables.tables.at(0);
        EXPECT_TRUE(first.continues_on_next_page);
        EXPECT_TRUE(second.continues_from_previous_page);
        EXPECT_EQ(first.continuation_group_id, second.continuation_group_id);
        EXPECT_FALSE(first.continuation_group_id.empty());
        EXPECT_DOUBLE_EQ(first.bbox.y0, 50);
        EXPECT_DOUBLE_EQ(second.bbox.y0, 48);
        EXPECT_DOUBLE_EQ(second.rows[0].cells[0].bbox.y0, 48);
        ASSERT_EQ(result.document.blocks.size(), 2U);
        EXPECT_DOUBLE_EQ(result.document.blocks[1].table_rows[0].cells[0].source_refs[0].bbox.y0, 48);
        EXPECT_EQ(result.document.blocks[1].table_rows[0].cells[0].row_index, 0);
        for (const auto& entry : std::filesystem::directory_iterator(options.output_directory))
            EXPECT_NE(entry.path().filename().string().find(".die-orientation-"), 0U);
    }
}
} // namespace
