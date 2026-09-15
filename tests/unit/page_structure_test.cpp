#include "assembly/document_assembler.h"
#include "pipeline/page_structure.h"
#include "reading_order/reading_order_backend.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

namespace {

using namespace doc_parser;

struct Fixture {
    document::PageText text;
    document::PageLayout layout;
    document::PageTables tables;
};

Fixture makeFixture() {
    Fixture fixture;
    for (int index = 0; index < 5; ++index) {
        fixture.text.lines.push_back({"row " + std::to_string(index),
                                      {100, 200.0 + index * 40, 400, 220.0 + index * 40},
                                      document::TextSource::PdfTextLayer,
                                      0.9,
                                      {}});
    }
    document::LayoutBlock figure;
    figure.id = "figure";
    figure.type = document::LayoutBlockType::Figure;
    figure.bbox = {90, 190, 410, 390};
    figure.confidence = 0.8;
    figure.text_line_indices = {0, 1, 2, 3, 4};
    auto table_block = figure;
    table_block.id = "table_block";
    table_block.type = document::LayoutBlockType::Table;
    table_block.bbox.y1 = 350;
    table_block.confidence = 0.95;
    table_block.text_line_indices = {0, 1, 2, 3};
    fixture.layout.blocks = {figure, table_block};
    document::Table table;
    table.id = "table";
    table.layout_block_id = table_block.id;
    table.bbox = table_block.bbox;
    table.rows.resize(1);
    table.rows[0].cells.resize(1);
    table.rows[0].cells[0].text = "cell text";
    table.rows[0].cells[0].bbox = table_block.bbox;
    fixture.tables.tables = {table};
    return fixture;
}

} // namespace

TEST(PageStructureTest, MergesAllSourceLinesAndIsIdempotentWithoutChangingDetectedCellGeometry) {
    auto fixture = makeFixture();
    const auto stats = pipeline::resolvePageStructure(fixture.text, fixture.layout, fixture.tables);
    EXPECT_EQ(stats.merged_figures, 1);
    EXPECT_EQ(stats.preserved_unique_lines, 1);
    ASSERT_EQ(fixture.layout.blocks.size(), 1U);
    EXPECT_EQ(fixture.layout.blocks[0].id, "table_block");
    EXPECT_EQ(fixture.layout.blocks[0].text_line_indices, (std::vector<int>{0, 1, 2, 3, 4}));
    EXPECT_DOUBLE_EQ(fixture.layout.blocks[0].bbox.y1, 380);
    EXPECT_DOUBLE_EQ(fixture.layout.blocks[0].confidence, 0.8);
    EXPECT_EQ(fixture.tables.tables[0].text_mode, document::TableTextMode::SourceLines);
    EXPECT_DOUBLE_EQ(fixture.tables.tables[0].bbox.y1, 350);
    EXPECT_DOUBLE_EQ(fixture.tables.tables[0].rows[0].cells[0].bbox.y1, 350);
    EXPECT_EQ(fixture.tables.tables[0].rows[0].cells[0].text, "cell text");
    const auto again = pipeline::resolvePageStructure(fixture.text, fixture.layout, fixture.tables);
    EXPECT_EQ(again.merged_figures, 0);
    EXPECT_EQ(again.preserved_unique_lines, 0);
    EXPECT_EQ(again.source_text_tables, 1);
}

TEST(PageStructureTest, RemapsCaptionThroughReadingOrderAndFinalDocumentRelation) {
    auto fixture = makeFixture();
    document::LayoutBlock caption;
    caption.id = "caption";
    caption.type = document::LayoutBlockType::Text;
    caption.source_label = "Caption";
    caption.related_block_id = "figure";
    caption.bbox = {100, 400, 400, 420};
    caption.text_line_indices = {5};
    fixture.text.lines.push_back({"caption", caption.bbox, document::TextSource::PdfTextLayer, 1.0, {}});
    fixture.layout.blocks.push_back(caption);
    pipeline::resolvePageStructure(fixture.text, fixture.layout, fixture.tables);
    ASSERT_EQ(fixture.layout.blocks.size(), 2U);
    EXPECT_EQ(fixture.layout.blocks[1].related_block_id, "table_block");
    document::PageArtifact page;
    page.page_number = 1;
    page.width = 1000;
    page.height = 1400;
    reading_order::ReadingOrderResult order;
    ASSERT_TRUE(reading_order::DoclingLikeReadingOrderBackend().order({page, fixture.layout}, order));
    ASSERT_EQ(order.reading_order.items.size(), 2U);
    EXPECT_EQ(order.reading_order.items[0].layout_block_id, "table_block");
    EXPECT_EQ(order.reading_order.trace.placements.back().parent_layout_block_id, "table_block");
    document::ParsedDocument output;
    document::PipelineArtifacts artifacts;
    ASSERT_TRUE(assembly::DocumentAssembler().assemble(
        {"fixture.pdf", "pdf", 144, {page}, {fixture.text}, {fixture.layout}, {order.reading_order}, {fixture.tables}},
        output,
        artifacts));
    ASSERT_EQ(output.blocks.size(), 2U);
    EXPECT_EQ(output.blocks[0].text, "row 0\nrow 1\nrow 2\nrow 3\nrow 4");
    EXPECT_EQ(output.blocks[0].source_refs[0].text, output.blocks[0].text);
    EXPECT_EQ(output.blocks[0].source_refs[0].source, document::TextSource::PdfTextLayer);
    ASSERT_EQ(output.relations.size(), 1U);
    EXPECT_EQ(output.relations[0].to_block_id, output.blocks[0].id);
    EXPECT_EQ(output.relations[0].from_block_id, output.blocks[1].id);
}

TEST(PageStructureTest, RetainsPartiallyOverlappingFiguresWithSubstantialUniqueText) {
    auto fixture = makeFixture();
    fixture.layout.blocks[1].text_line_indices = {0, 1};
    EXPECT_EQ(pipeline::resolvePageStructure(fixture.text, fixture.layout, fixture.tables).merged_figures, 0);
    ASSERT_EQ(fixture.layout.blocks.size(), 2U);
    EXPECT_EQ(fixture.layout.blocks[0].text_line_indices, (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(PageStructureTest, DoesNotResolveCompetingTablesByInputOrder) {
    for (bool reverse : {false, true}) {
        auto fixture = makeFixture();
        auto block = fixture.layout.blocks[1];
        block.id = "other_table_block";
        auto table = fixture.tables.tables[0];
        table.id = "other_table";
        table.layout_block_id = block.id;
        fixture.tables.tables.push_back(table);
        fixture.layout.blocks.push_back(block);
        if (reverse)
            std::reverse(fixture.layout.blocks.begin(), fixture.layout.blocks.end());
        EXPECT_EQ(pipeline::resolvePageStructure(fixture.text, fixture.layout, fixture.tables).merged_figures, 0);
        EXPECT_EQ(fixture.layout.blocks.size(), 3U);
    }
}

TEST(PageStructureTest, DoesNotCountDuplicateOrInvalidLineIndicesAsOverlapEvidence) {
    for (const auto& indices : std::vector<std::vector<int>>{{0, 0, 0, 0, 4}, {0, 1, 2, 3, 99}, {-1, 0, 1, 2, 3}}) {
        auto fixture = makeFixture();
        fixture.layout.blocks[0].text_line_indices = indices;
        EXPECT_EQ(pipeline::resolvePageStructure(fixture.text, fixture.layout, fixture.tables).merged_figures, 0);
        EXPECT_EQ(fixture.layout.blocks.size(), 2U);
    }
    auto fixture = makeFixture();
    fixture.layout.blocks[0].bbox.x0 = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(pipeline::resolvePageStructure(fixture.text, fixture.layout, fixture.tables).merged_figures, 0);
}

TEST(PageStructureTest, PreservesSourceForEmptyGridButKeepsCellOnlyRecognition) {
    for (bool no_rows : {false, true}) {
        auto fixture = makeFixture();
        fixture.layout.blocks.erase(fixture.layout.blocks.begin());
        if (no_rows)
            fixture.tables.tables[0].rows.clear();
        else
            fixture.tables.tables[0].rows[0].cells[0].text = " \t";
        pipeline::resolvePageStructure(fixture.text, fixture.layout, fixture.tables);
        EXPECT_EQ(fixture.tables.tables[0].text_mode, document::TableTextMode::SourceLines);
    }
    auto fixture = makeFixture();
    fixture.layout.blocks.erase(fixture.layout.blocks.begin());
    fixture.layout.blocks[0].text_line_indices.clear();
    pipeline::resolvePageStructure(fixture.text, fixture.layout, fixture.tables);
    EXPECT_EQ(fixture.tables.tables[0].text_mode, document::TableTextMode::Cells);
}

TEST(PageStructureTest, AppliesExistingFormPolicyBeforeOrderingAndKeepsCompleteGridText) {
    auto fixture = makeFixture();
    fixture.layout.blocks.erase(fixture.layout.blocks.begin());
    pipeline::resolvePageStructure(fixture.text, fixture.layout, fixture.tables);
    EXPECT_EQ(fixture.tables.tables[0].text_mode, document::TableTextMode::SourceLines);
    fixture = makeFixture();
    fixture.layout.blocks.erase(fixture.layout.blocks.begin());
    fixture.tables.tables[0].rows[0].cells[0].text = "row 0 row 1 row 2 row 3";
    pipeline::resolvePageStructure(fixture.text, fixture.layout, fixture.tables);
    EXPECT_EQ(fixture.tables.tables[0].text_mode, document::TableTextMode::Cells);
}
