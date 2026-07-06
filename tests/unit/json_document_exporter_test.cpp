#include "export/json_document_exporter.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

using doc_parser::document::BBox;
using doc_parser::document::DebugImageArtifact;
using doc_parser::document::DocumentBlock;
using doc_parser::document::DocumentBlockType;
using doc_parser::document::LayoutBlock;
using doc_parser::document::LayoutBlockType;
using doc_parser::document::PageArtifact;
using doc_parser::document::PageLayout;
using doc_parser::document::PageTables;
using doc_parser::document::PageText;
using doc_parser::document::ParsedDocument;
using doc_parser::document::ParsedPage;
using doc_parser::document::Table;
using doc_parser::document::TableCell;
using doc_parser::document::TableRow;
using doc_parser::document::TextLine;
using doc_parser::document::TextSource;
using doc_parser::document::TextSpan;
using doc_parser::exporter::JsonDocumentExporter;

std::filesystem::path tempManifestPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

ParsedDocument makeDocument() {
    TextSpan span;
    span.text = "Table";
    span.bbox = BBox{0.0, 1.0, 2.0, 3.0};
    span.source = TextSource::PdfTextLayer;
    span.confidence = 1.0;

    TextLine line;
    line.text = "Table";
    line.bbox = span.bbox;
    line.source = span.source;
    line.confidence = span.confidence;
    line.spans.push_back(span);

    PageText text;
    text.page_index = 0;
    text.page_number = 1;
    text.has_text = true;
    text.preferred_source = TextSource::PdfTextLayer;
    text.lines.push_back(line);

    PageArtifact image;
    image.page_index = 0;
    image.page_number = 1;
    image.relative_image = "pages/page_1.png";
    image.output_path = "/tmp/pages/page_1.png";
    image.width = 100;
    image.height = 200;
    image.debug_images.push_back(DebugImageArtifact{
        "preprocessed",
        "debug/page_1_preprocessed.png",
        "/tmp/debug/page_1_preprocessed.png",
    });

    LayoutBlock block;
    block.id = "page_1_block_1";
    block.type = LayoutBlockType::Text;
    block.bbox = line.bbox;
    block.confidence = 0.75;
    block.text_line_indices.push_back(0);

    PageLayout layout;
    layout.page_index = 0;
    layout.page_number = 1;
    layout.blocks.push_back(block);

    TableCell cell;
    cell.row_index = 0;
    cell.column_index = 0;
    cell.text = "Table";
    cell.bbox = line.bbox;
    cell.confidence = 0.9;

    TableRow row;
    row.row_index = 0;
    row.cells.push_back(cell);

    Table table;
    table.id = "page_1_table_1";
    table.layout_block_id = block.id;
    table.page_index = 0;
    table.page_number = 1;
    table.bbox = line.bbox;
    table.confidence = 0.8;
    table.rows.push_back(row);

    PageTables tables;
    tables.page_index = 0;
    tables.page_number = 1;
    tables.tables.push_back(table);

    ParsedDocument document;
    document.source.path = "fixture.pdf";
    document.source.type = "pdf";
    document.dpi = 144;
    DocumentBlock document_block;
    document_block.id = "doc_page_1_block_1";
    document_block.type = DocumentBlockType::Paragraph;
    document_block.page_index = 0;
    document_block.page_number = 1;
    document_block.bbox = line.bbox;
    document_block.confidence = 0.75;
    document_block.text = "Table";
    document_block.text_line_indices.push_back(0);
    document.blocks.push_back(document_block);

    DocumentBlock table_block;
    table_block.id = "doc_page_1_block_2";
    table_block.type = DocumentBlockType::Table;
    table_block.page_index = 0;
    table_block.page_number = 1;
    table_block.bbox = table.bbox;
    table_block.confidence = table.confidence;
    table_block.text = "Table";
    table_block.table_id = table.id;
    table_block.table_rows = table.rows;
    table_block.text_line_indices.push_back(0);
    document.blocks.push_back(table_block);

    document.pages.push_back(ParsedPage{
        0,
        1,
        image,
        text,
        layout,
        tables,
    });
    return document;
}

nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    return nlohmann::json::parse(input);
}

} // namespace

TEST(JsonDocumentExporterTest, WritesManifestWithoutDebugFieldsByDefault) {
    const auto output_path = tempManifestPath("tdp_json_document_exporter_normal_test.json");
    std::filesystem::remove(output_path);

    const ParsedDocument document = makeDocument();
    ASSERT_TRUE(JsonDocumentExporter().write({
        false,
        output_path,
        &document,
    }));

    const auto manifest = readJson(output_path);
    EXPECT_EQ(manifest["source"]["path"], "fixture.pdf");
    EXPECT_EQ(manifest["source"]["type"], "pdf");
    EXPECT_EQ(manifest["render"]["dpi"], 144);
    ASSERT_EQ(manifest["blocks"].size(), 2U);
    EXPECT_EQ(manifest["blocks"][0]["id"], "doc_page_1_block_1");
    EXPECT_EQ(manifest["blocks"][0]["type"], "paragraph");
    EXPECT_EQ(manifest["blocks"][0]["text"], "Table");
    EXPECT_FALSE(manifest["blocks"][0].contains("rows"));
    EXPECT_EQ(manifest["blocks"][1]["id"], "doc_page_1_block_2");
    EXPECT_EQ(manifest["blocks"][1]["type"], "table");
    EXPECT_EQ(manifest["blocks"][1]["table_id"], "page_1_table_1");
    ASSERT_EQ(manifest["blocks"][1]["rows"].size(), 1U);
    ASSERT_EQ(manifest["blocks"][1]["rows"][0]["cells"].size(), 1U);
    EXPECT_EQ(manifest["blocks"][1]["rows"][0]["cells"][0]["text"], "Table");
    ASSERT_EQ(manifest["pages"].size(), 1U);
    EXPECT_EQ(manifest["pages"][0]["page_index"], 0);
    EXPECT_EQ(manifest["pages"][0]["page_number"], 1);
    EXPECT_EQ(manifest["pages"][0]["image"], "pages/page_1.png");
    EXPECT_FALSE(manifest["pages"][0].contains("debug"));

    std::filesystem::remove(output_path);
}

TEST(JsonDocumentExporterTest, WritesDebugTextAndImagesWhenRequested) {
    const auto output_path = tempManifestPath("tdp_json_document_exporter_debug_test.json");
    std::filesystem::remove(output_path);

    const ParsedDocument document = makeDocument();
    ASSERT_TRUE(JsonDocumentExporter().write({
        true,
        output_path,
        &document,
    }));

    const auto manifest = readJson(output_path);
    const auto& debug = manifest["pages"][0]["debug"];
    EXPECT_TRUE(debug["text"]["has_text"]);
    EXPECT_EQ(debug["text"]["preferred_source"], "pdf_text_layer");
    ASSERT_EQ(debug["text"]["lines"].size(), 1U);
    EXPECT_EQ(debug["text"]["lines"][0]["text"], "Table");
    ASSERT_EQ(debug["text"]["lines"][0]["spans"].size(), 1U);
    EXPECT_EQ(debug["text"]["lines"][0]["spans"][0]["text"], "Table");
    ASSERT_EQ(debug["layout"]["blocks"].size(), 1U);
    EXPECT_EQ(debug["layout"]["blocks"][0]["id"], "page_1_block_1");
    EXPECT_EQ(debug["layout"]["blocks"][0]["type"], "text");
    ASSERT_EQ(debug["layout"]["blocks"][0]["text_line_indices"].size(), 1U);
    EXPECT_EQ(debug["layout"]["blocks"][0]["text_line_indices"][0], 0);
    ASSERT_EQ(debug["tables"]["tables"].size(), 1U);
    EXPECT_EQ(debug["tables"]["tables"][0]["id"], "page_1_table_1");
    EXPECT_EQ(debug["tables"]["tables"][0]["layout_block_id"], "page_1_block_1");
    ASSERT_EQ(debug["tables"]["tables"][0]["rows"].size(), 1U);
    ASSERT_EQ(debug["tables"]["tables"][0]["rows"][0]["cells"].size(), 1U);
    EXPECT_EQ(debug["tables"]["tables"][0]["rows"][0]["cells"][0]["text"], "Table");
    ASSERT_EQ(debug["images"].size(), 1U);
    EXPECT_EQ(debug["images"][0]["name"], "preprocessed");
    EXPECT_EQ(debug["images"][0]["image"], "debug/page_1_preprocessed.png");

    std::filesystem::remove(output_path);
}

TEST(JsonDocumentExporterTest, RejectsMissingDocument) {
    const auto output_path = tempManifestPath("tdp_json_document_exporter_missing_document_test.json");

    EXPECT_FALSE(JsonDocumentExporter().write({
        false,
        output_path,
        nullptr,
    }));
}
