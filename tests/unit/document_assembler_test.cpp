#include "assembly/document_assembler.h"

#include <gtest/gtest.h>

namespace {

doc_parser::document::TextSpan makeSpan(const std::string& text, doc_parser::document::BBox bbox) {
    doc_parser::document::TextSpan span;
    span.text = text;
    span.bbox = bbox;
    span.source = doc_parser::document::TextSource::PdfTextLayer;
    span.confidence = 0.9;
    return span;
}

doc_parser::document::TextLine makeLine(const std::string& text, doc_parser::document::BBox bbox) {
    doc_parser::document::TextLine line;
    line.text = text;
    line.bbox = bbox;
    line.source = doc_parser::document::TextSource::PdfTextLayer;
    line.confidence = 0.9;
    line.spans.push_back(makeSpan(text, bbox));
    return line;
}

doc_parser::document::PageArtifact makePage() {
    doc_parser::document::PageArtifact page;
    page.page_index = 0;
    page.page_number = 1;
    page.relative_image = "pages/page_1.png";
    page.width = 1000;
    page.height = 1400;
    return page;
}

doc_parser::document::PageText makePageText() {
    doc_parser::document::PageText text;
    text.page_index = 0;
    text.page_number = 1;
    text.has_text = true;
    text.preferred_source = doc_parser::document::TextSource::PdfTextLayer;
    text.lines.push_back(makeLine("Overview", {100.0, 100.0, 300.0, 130.0}));
    text.lines.push_back(makeLine("Chapter 1 ................................ 2", {100.0, 200.0, 900.0, 230.0}));
    return text;
}

doc_parser::document::PageLayout makePageLayout() {
    doc_parser::document::LayoutBlock title;
    title.id = "page_1_block_1";
    title.type = doc_parser::document::LayoutBlockType::Title;
    title.bbox = {100.0, 100.0, 300.0, 130.0};
    title.confidence = 0.7;
    title.text_line_indices.push_back(0);

    doc_parser::document::LayoutBlock table;
    table.id = "page_1_block_2";
    table.type = doc_parser::document::LayoutBlockType::Table;
    table.bbox = {100.0, 200.0, 900.0, 230.0};
    table.confidence = 0.6;
    table.text_line_indices.push_back(1);

    doc_parser::document::PageLayout layout;
    layout.page_index = 0;
    layout.page_number = 1;
    layout.blocks = {title, table};
    return layout;
}

doc_parser::document::PageTables makePageTables() {
    doc_parser::document::TableCell left;
    left.row_index = 0;
    left.column_index = 0;
    left.text = "Chapter 1";
    left.bbox = {100.0, 200.0, 220.0, 230.0};
    left.confidence = 0.9;

    doc_parser::document::TableCell right;
    right.row_index = 0;
    right.column_index = 1;
    right.text = "2";
    right.bbox = {880.0, 200.0, 900.0, 230.0};
    right.confidence = 0.9;

    doc_parser::document::TableRow row;
    row.row_index = 0;
    row.cells = {left, right};

    doc_parser::document::Table table;
    table.id = "page_1_table_1";
    table.layout_block_id = "page_1_block_2";
    table.page_index = 0;
    table.page_number = 1;
    table.bbox = {100.0, 200.0, 900.0, 230.0};
    table.confidence = 0.6;
    table.rows.push_back(row);

    doc_parser::document::PageTables tables;
    tables.page_index = 0;
    tables.page_number = 1;
    tables.tables.push_back(table);
    return tables;
}

} // namespace

TEST(DocumentAssemblerTest, BuildsDocumentBlocksFromLayoutAndTables) {
    const doc_parser::assembly::DocumentAssembler assembler;
    doc_parser::document::ParsedDocument document;
    doc_parser::document::PipelineArtifacts artifacts;

    ASSERT_TRUE(assembler.assemble(
        {
            "fixture.pdf",
            "pdf",
            144,
            {makePage()},
            {makePageText()},
            {makePageLayout()},
            {makePageTables()},
        },
        document,
        artifacts));

    EXPECT_EQ(document.source.path, "fixture.pdf");
    EXPECT_EQ(document.dpi, 144);
    ASSERT_EQ(artifacts.pages.size(), 1U);
    ASSERT_EQ(document.blocks.size(), 2U);

    EXPECT_EQ(document.blocks[0].type, doc_parser::document::DocumentBlockType::Title);
    EXPECT_EQ(document.blocks[0].text, "Overview");
    EXPECT_EQ(document.blocks[0].page_number, 1);

    EXPECT_EQ(document.blocks[1].type, doc_parser::document::DocumentBlockType::Table);
    EXPECT_EQ(document.blocks[1].table_id, "page_1_table_1");
    EXPECT_EQ(document.blocks[1].text, "Chapter 1\t2");
    ASSERT_EQ(document.blocks[1].table_rows.size(), 1U);
    ASSERT_EQ(document.blocks[1].table_rows[0].cells.size(), 2U);
    EXPECT_EQ(document.blocks[1].table_rows[0].cells[0].text, "Chapter 1");
}

TEST(DocumentAssemblerTest, RejectsMismatchedPageCounts) {
    const doc_parser::assembly::DocumentAssembler assembler;
    doc_parser::document::ParsedDocument document;
    doc_parser::document::PipelineArtifacts artifacts;

    EXPECT_FALSE(assembler.assemble(
        {
            "fixture.pdf",
            "pdf",
            144,
            {makePage()},
            {},
            {makePageLayout()},
            {makePageTables()},
        },
        document,
        artifacts));
}
