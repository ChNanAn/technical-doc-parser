#include "pipeline/table_recognition_stage.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <vector>

namespace {

class CrossPageTableBackend final : public doc_parser::table::ITableBackend {
public:
    bool recognize(const doc_parser::table::TableRequest& request,
                   doc_parser::table::TableResult& result) const override {
        result = {};
        result.tables.page_index = request.page.page_index;
        result.tables.page_number = request.page.page_number;

        doc_parser::document::Table table;
        table.id = "page_" + std::to_string(request.page.page_number) + "_table_1";
        table.page_index = request.page.page_index;
        table.page_number = request.page.page_number;
        table.source_label = "table";
        table.confidence = 0.95;
        table.bbox = request.page.page_number == 1 ? doc_parser::document::BBox{100.0, 1100.0, 900.0, 1390.0}
                                                   : doc_parser::document::BBox{100.0, 10.0, 900.0, 500.0};
        table.columns.push_back({0, {100.0, table.bbox.y0, 500.0, table.bbox.y1}, 0.9});
        table.columns.push_back({1, {500.0, table.bbox.y0, 900.0, table.bbox.y1}, 0.9});
        result.tables.tables.push_back(std::move(table));
        return true;
    }
};

doc_parser::document::PageArtifact makePage(int page_index) {
    doc_parser::document::PageArtifact page;
    page.page_index = page_index;
    page.page_number = page_index + 1;
    page.output_path = std::filesystem::path("/tmp/page_" + std::to_string(page.page_number) + ".png");
    page.width = 1000;
    page.height = 1400;
    return page;
}

} // namespace

TEST(TableRecognitionStageTest, AddsDetectedRegionsToLayoutAndLinksCrossPageTables) {
    const CrossPageTableBackend backend;
    const doc_parser::pipeline::TableRecognitionStage stage(backend);
    const std::vector<doc_parser::document::PageArtifact> pages{makePage(0), makePage(1)};
    std::vector<doc_parser::document::PageText> texts(2);
    std::vector<doc_parser::document::PageLayout> layouts(2);
    for (int index = 0; index < 2; ++index) {
        texts[static_cast<std::size_t>(index)].page_index = index;
        texts[static_cast<std::size_t>(index)].page_number = index + 1;
        layouts[static_cast<std::size_t>(index)].page_index = index;
        layouts[static_cast<std::size_t>(index)].page_number = index + 1;
    }

    doc_parser::pipeline::PipelineContext context;
    std::vector<doc_parser::document::PageTables> tables;
    ASSERT_TRUE(stage.recognize(context, pages, texts, layouts, tables).okStatus());

    ASSERT_EQ(layouts[0].blocks.size(), 1U);
    EXPECT_EQ(layouts[0].blocks[0].type, doc_parser::document::LayoutBlockType::Table);
    EXPECT_EQ(tables[0].tables[0].layout_block_id, layouts[0].blocks[0].id);
    ASSERT_EQ(tables.size(), 2U);
    EXPECT_TRUE(tables[0].tables[0].continues_on_next_page);
    EXPECT_TRUE(tables[1].tables[0].continues_from_previous_page);
    EXPECT_FALSE(tables[0].tables[0].continuation_group_id.empty());
    EXPECT_EQ(tables[0].tables[0].continuation_group_id, tables[1].tables[0].continuation_group_id);
}
