#include "table/table_backend.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

doc_parser::document::PageArtifact makePage() {
    doc_parser::document::PageArtifact page;
    page.page_index = 0;
    page.page_number = 1;
    page.output_path = std::filesystem::path("/tmp/page_1.png");
    page.width = 1000;
    page.height = 1400;
    return page;
}

doc_parser::document::TextSpan makeSpan(const std::string& text, doc_parser::document::BBox bbox) {
    doc_parser::document::TextSpan span;
    span.text = text;
    span.bbox = bbox;
    span.source = doc_parser::document::TextSource::PdfTextLayer;
    span.confidence = 0.95;
    return span;
}

doc_parser::document::TextLine makeTocLine() {
    doc_parser::document::TextLine line;
    line.source = doc_parser::document::TextSource::PdfTextLayer;
    line.confidence = 0.9;
    line.text = "Chapter 1 ................................ 2";
    line.spans.push_back(makeSpan("Chapter", {100.0, 200.0, 180.0, 225.0}));
    line.spans.push_back(makeSpan("1", {190.0, 200.0, 205.0, 225.0}));
    line.spans.push_back(makeSpan("................................", {260.0, 215.0, 800.0, 220.0}));
    line.spans.push_back(makeSpan("2", {900.0, 200.0, 915.0, 225.0}));
    line.bbox = {100.0, 200.0, 915.0, 225.0};
    return line;
}

doc_parser::document::PageLayout makeLayout() {
    doc_parser::document::LayoutBlock block;
    block.id = "page_1_block_1";
    block.type = doc_parser::document::LayoutBlockType::Table;
    block.bbox = {100.0, 200.0, 915.0, 225.0};
    block.confidence = 0.6;
    block.text_line_indices.push_back(0);

    doc_parser::document::PageLayout layout;
    layout.page_index = 0;
    layout.page_number = 1;
    layout.blocks.push_back(block);
    return layout;
}

} // namespace

TEST(TableBackendTest, BuildsRowsAndCellsFromTableLayoutBlock) {
    doc_parser::document::PageText text;
    text.page_index = 0;
    text.page_number = 1;
    text.has_text = true;
    text.preferred_source = doc_parser::document::TextSource::PdfTextLayer;
    text.lines.push_back(makeTocLine());

    const doc_parser::table::TextTableStructureBackend backend;
    doc_parser::table::TableResult result;
    ASSERT_TRUE(backend.recognize({makePage(), text, makeLayout()}, result));
    const doc_parser::document::PageTables& tables = result.tables;

    ASSERT_EQ(tables.tables.size(), 1U);
    EXPECT_EQ(tables.tables[0].layout_block_id, "page_1_block_1");
    ASSERT_EQ(tables.tables[0].rows.size(), 1U);
    ASSERT_EQ(tables.tables[0].rows[0].cells.size(), 2U);
    EXPECT_EQ(tables.tables[0].rows[0].cells[0].text, "Chapter 1");
    EXPECT_EQ(tables.tables[0].rows[0].cells[1].text, "2");
}

TEST(TableBackendTest, IgnoresPagesWithoutTableBlocks) {
    doc_parser::document::PageText text;
    text.page_index = 0;
    text.page_number = 1;

    doc_parser::document::PageLayout layout;
    layout.page_index = 0;
    layout.page_number = 1;

    const doc_parser::table::TextTableStructureBackend backend;
    doc_parser::table::TableResult result;
    ASSERT_TRUE(backend.recognize({makePage(), text, layout}, result));
    const doc_parser::document::PageTables& tables = result.tables;
    EXPECT_TRUE(tables.tables.empty());
}
