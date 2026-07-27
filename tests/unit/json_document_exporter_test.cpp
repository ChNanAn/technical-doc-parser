#include "common/warning_codes.h"
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
using doc_parser::document::PageReadingOrder;
using doc_parser::document::PageTables;
using doc_parser::document::PageText;
using doc_parser::document::ParsedDocument;
using doc_parser::document::PipelineArtifacts;
using doc_parser::document::PipelinePageArtifacts;
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

struct DocumentFixture {
    ParsedDocument document;
    PipelineArtifacts artifacts;
};

DocumentFixture makeDocumentFixture() {
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

    PageReadingOrder reading_order;
    reading_order.page_index = 0;
    reading_order.page_number = 1;
    reading_order.items.push_back({block.id, 0, 0});

    TableCell cell;
    cell.row_index = 0;
    cell.column_index = 0;
    cell.text = "Table";
    cell.bbox = line.bbox;
    cell.confidence = 0.9;
    cell.source_refs.push_back({"page_1", cell.bbox, cell.text, TextSource::PdfTextLayer});

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
    document.document_id = "fixture_document";
    document.source.path = "/private/input/fixture.pdf";
    document.source.type = "pdf";
    document.source.filename = "fixture.pdf";
    document.source.media_type = "application/pdf";
    document.source.size_bytes = 123;
    document.source.sha256 = std::string(64, 'a');
    document.producer.version = "test";
    document.dpi = 144;
    document.pages.push_back({
        "page_1",
        1,
        100.0,
        200.0,
        "page_1_image",
        "pages/page_1.png",
        "image/png",
    });
    DocumentBlock document_block;
    document_block.id = "doc_page_1_block_1";
    document_block.type = DocumentBlockType::Paragraph;
    document_block.page_index = 0;
    document_block.page_number = 1;
    document_block.page_id = "page_1";
    document_block.bbox = line.bbox;
    document_block.confidence = 0.75;
    document_block.text = "Table";
    document_block.source_refs.push_back(
        {document_block.page_id, document_block.bbox, document_block.text, TextSource::PdfTextLayer});
    document_block.text_line_indices.push_back(0);
    document.blocks.push_back(document_block);

    DocumentBlock table_block;
    table_block.id = "doc_page_1_block_2";
    table_block.type = DocumentBlockType::Table;
    table_block.page_index = 0;
    table_block.page_number = 1;
    table_block.page_id = "page_1";
    table_block.bbox = table.bbox;
    table_block.confidence = table.confidence;
    table_block.text = "Table";
    table_block.source_refs.push_back(
        {table_block.page_id, table_block.bbox, table_block.text, TextSource::PdfTextLayer});
    table_block.table_id = table.id;
    table_block.table_rows = table.rows;
    table_block.text_line_indices.push_back(0);
    document.blocks.push_back(table_block);
    document.relations.push_back({
        "relation_1",
        "related_to",
        document_block.id,
        table_block.id,
    });

    PipelineArtifacts artifacts;
    artifacts.pages.push_back(PipelinePageArtifacts{
        0,
        1,
        image,
        text,
        layout,
        reading_order,
        tables,
    });
    return {
        document,
        artifacts,
    };
}

nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    return nlohmann::json::parse(input);
}

} // namespace

TEST(JsonDocumentExporterTest, WritesManifestWithoutDebugFieldsByDefault) {
    const auto output_path = tempManifestPath("tdp_json_document_exporter_normal_test.json");
    std::filesystem::remove(output_path);

    const DocumentFixture fixture = makeDocumentFixture();
    ASSERT_TRUE(JsonDocumentExporter()
                    .write({
                        false,
                        output_path,
                        &fixture.document,
                        &fixture.artifacts,
                    })
                    .okStatus());

    const auto manifest = readJson(output_path);
    EXPECT_EQ(manifest["$schema"], "https://github.com/ChNanAn/technical-doc-parser/schemas/document.v1.schema.json");
    EXPECT_EQ(manifest["schema_version"], 1);
    EXPECT_EQ(manifest["document_id"], "fixture_document");
    EXPECT_EQ(manifest["status"], "complete");
    EXPECT_EQ(manifest["source"]["filename"], "fixture.pdf");
    EXPECT_EQ(manifest["source"]["media_type"], "application/pdf");
    EXPECT_EQ(manifest["producer"]["name"], "technical-doc-parser");
    EXPECT_EQ(manifest["producer"]["version"], "test");
    EXPECT_EQ(manifest["coordinate_space"]["unit"], "pixel");
    EXPECT_EQ(manifest["coordinate_space"]["origin"], "top_left");
    EXPECT_EQ(manifest["coordinate_space"]["bbox_format"], "xyxy");
    EXPECT_EQ(manifest["coordinate_space"]["dpi"], 144);
    EXPECT_EQ(manifest.dump().find("/private/input"), std::string::npos);
    ASSERT_EQ(manifest["blocks"].size(), 2U);
    EXPECT_EQ(manifest["blocks"][0]["id"], "doc_page_1_block_1");
    EXPECT_EQ(manifest["blocks"][0]["type"], "paragraph");
    EXPECT_EQ(manifest["blocks"][0]["text"], "Table");
    EXPECT_EQ(manifest["blocks"][0]["page_id"], "page_1");
    EXPECT_EQ(manifest["blocks"][0]["bbox"], nlohmann::json::array({0.0, 1.0, 2.0, 3.0}));
    EXPECT_EQ(manifest["blocks"][0]["score"]["value"], 0.75);
    EXPECT_EQ(manifest["blocks"][0]["source_refs"][0]["kind"], "pdf_text_layer");
    EXPECT_FALSE(manifest["blocks"][0].contains("table"));
    EXPECT_EQ(manifest["blocks"][1]["id"], "doc_page_1_block_2");
    EXPECT_EQ(manifest["blocks"][1]["type"], "table");
    EXPECT_EQ(manifest["blocks"][1]["table"]["id"], "page_1_table_1");
    ASSERT_EQ(manifest["blocks"][1]["table"]["rows"].size(), 1U);
    ASSERT_EQ(manifest["blocks"][1]["table"]["rows"][0]["cells"].size(), 1U);
    EXPECT_EQ(manifest["blocks"][1]["table"]["rows"][0]["cells"][0]["text"], "Table");
    EXPECT_EQ(manifest["blocks"][1]["table"]["rows"][0]["cells"][0]["source_refs"][0]["page_id"], "page_1");
    ASSERT_EQ(manifest["pages"].size(), 1U);
    EXPECT_EQ(manifest["pages"][0]["id"], "page_1");
    EXPECT_EQ(manifest["pages"][0]["number"], 1);
    EXPECT_EQ(manifest["pages"][0]["width"], 100.0);
    EXPECT_EQ(manifest["pages"][0]["height"], 200.0);
    EXPECT_EQ(manifest["pages"][0]["image"]["uri"], "pages/page_1.png");
    EXPECT_FALSE(manifest["pages"][0].contains("extensions"));
    EXPECT_TRUE(manifest["warnings"].empty());
    EXPECT_EQ(manifest["source"]["size_bytes"], 123);
    EXPECT_EQ(manifest["source"]["sha256"], std::string(64, 'a'));
    ASSERT_EQ(manifest["relations"].size(), 1U);
    EXPECT_EQ(manifest["relations"][0]["from_block_id"], "doc_page_1_block_1");
    EXPECT_EQ(manifest["relations"][0]["to_block_id"], "doc_page_1_block_2");

    std::filesystem::remove(output_path);
}

TEST(JsonDocumentExporterTest, WritesDebugTextAndImagesWhenRequested) {
    const auto output_path = tempManifestPath("tdp_json_document_exporter_debug_test.json");
    std::filesystem::remove(output_path);

    const DocumentFixture fixture = makeDocumentFixture();
    ASSERT_TRUE(JsonDocumentExporter()
                    .write({
                        true,
                        output_path,
                        &fixture.document,
                        &fixture.artifacts,
                    })
                    .okStatus());

    const auto manifest = readJson(output_path);
    const auto& debug = manifest["pages"][0]["extensions"]["io.github.chnanan.technical-doc-parser.pipeline_debug"];
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
    ASSERT_EQ(debug["reading_order"]["items"].size(), 1U);
    EXPECT_EQ(debug["reading_order"]["items"][0]["layout_block_id"], "page_1_block_1");
    EXPECT_EQ(debug["reading_order"]["items"][0]["layout_block_index"], 0);
    EXPECT_EQ(debug["reading_order"]["items"][0]["sequence_index"], 0);
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

TEST(JsonDocumentExporterTest, WritesMixedPreferredTextSource) {
    const auto output_path = tempManifestPath("tdp_json_document_exporter_mixed_source_test.json");
    std::filesystem::remove(output_path);

    DocumentFixture fixture = makeDocumentFixture();
    fixture.artifacts.pages[0].text.preferred_source = TextSource::Mixed;
    ASSERT_TRUE(JsonDocumentExporter()
                    .write({
                        true,
                        output_path,
                        &fixture.document,
                        &fixture.artifacts,
                    })
                    .okStatus());

    const auto manifest = readJson(output_path);
    const auto& debug = manifest["pages"][0]["extensions"]["io.github.chnanan.technical-doc-parser.pipeline_debug"];
    EXPECT_EQ(debug["text"]["preferred_source"], "mixed");

    std::filesystem::remove(output_path);
}

TEST(JsonDocumentExporterTest, RejectsMissingDocument) {
    const auto output_path = tempManifestPath("tdp_json_document_exporter_missing_document_test.json");

    const doc_parser::common::Status status = JsonDocumentExporter().write({
        false,
        output_path,
        nullptr,
    });

    EXPECT_FALSE(status.okStatus());
    EXPECT_EQ(status.stage(), "export");
    EXPECT_EQ(status.code(), "export.json.document_missing");
    EXPECT_EQ(status.message(), "document export request has no document");
}

TEST(JsonDocumentExporterTest, RejectsUnexplainedPartialDocument) {
    const auto output_path = tempManifestPath("tdp_json_document_exporter_partial_test.json");
    std::filesystem::remove(output_path);
    DocumentFixture fixture = makeDocumentFixture();
    fixture.document.status = doc_parser::document::DocumentStatus::Partial;

    const doc_parser::common::Status status = JsonDocumentExporter().write({
        false,
        output_path,
        &fixture.document,
        &fixture.artifacts,
    });

    EXPECT_FALSE(status.okStatus());
    EXPECT_EQ(status.stage(), "export");
    EXPECT_EQ(status.code(), "export.document.partial_unexplained");
    EXPECT_EQ(status.message(), "partial document has no warning explaining the degraded result");
    EXPECT_FALSE(std::filesystem::exists(output_path));
}

TEST(JsonDocumentExporterTest, WritesExplainedPartialDocument) {
    const auto output_path = tempManifestPath("tdp_json_document_exporter_explained_partial_test.json");
    std::filesystem::remove(output_path);
    DocumentFixture fixture = makeDocumentFixture();
    fixture.document.status = doc_parser::document::DocumentStatus::Partial;
    fixture.document.warnings.push_back({
        doc_parser::common::warning_codes::kOcrEnhancementFailed,
        "OCR enhancement failed; retained usable native text",
        "text",
        "page_1",
        "doc_page_1_block_1",
        {{"backend", "paddle"}},
    });

    ASSERT_TRUE(JsonDocumentExporter()
                    .write({
                        false,
                        output_path,
                        &fixture.document,
                        &fixture.artifacts,
                    })
                    .okStatus());
    const auto manifest = readJson(output_path);
    EXPECT_EQ(manifest["status"], "partial");
    ASSERT_EQ(manifest["warnings"].size(), 1U);
    EXPECT_EQ(manifest["warnings"][0]["code"], doc_parser::common::warning_codes::kOcrEnhancementFailed);
    EXPECT_EQ(manifest["warnings"][0]["page_id"], "page_1");
    EXPECT_EQ(manifest["warnings"][0]["block_id"], "doc_page_1_block_1");
    EXPECT_EQ(manifest["warnings"][0]["details"]["backend"], "paddle");

    std::filesystem::remove(output_path);
}

TEST(JsonDocumentExporterTest, OmitsUnsafeArtifactsAndOutOfPageCoordinates) {
    const auto output_path = tempManifestPath("tdp_json_document_exporter_sanitized_test.json");
    std::filesystem::remove(output_path);
    DocumentFixture fixture = makeDocumentFixture();
    fixture.document.pages[0].image_uri = "/private/output/page_1.png";
    fixture.document.blocks[0].bbox.x1 = 101.0;
    fixture.document.blocks[0].source_refs[0].bbox.x1 = 101.0;

    ASSERT_TRUE(JsonDocumentExporter()
                    .write({
                        false,
                        output_path,
                        &fixture.document,
                        &fixture.artifacts,
                    })
                    .okStatus());
    const auto manifest = readJson(output_path);
    EXPECT_FALSE(manifest["pages"][0].contains("image"));
    EXPECT_FALSE(manifest["blocks"][0].contains("bbox"));
    EXPECT_FALSE(manifest["blocks"][0]["source_refs"][0].contains("bbox"));
    EXPECT_EQ(manifest.dump().find("/private/output"), std::string::npos);

    std::filesystem::remove(output_path);
}

TEST(JsonDocumentExporterTest, RejectsSelfReferentialRelationWithSpecificDiagnostic) {
    const auto output_path = tempManifestPath("tdp_json_document_exporter_self_relation_test.json");
    std::filesystem::remove(output_path);
    DocumentFixture fixture = makeDocumentFixture();
    fixture.document.relations[0].to_block_id = fixture.document.relations[0].from_block_id;

    const doc_parser::common::Status status = JsonDocumentExporter().write({
        false,
        output_path,
        &fixture.document,
        &fixture.artifacts,
    });

    EXPECT_FALSE(status.okStatus());
    EXPECT_EQ(status.stage(), "export");
    EXPECT_EQ(status.code(), "export.document.invalid_relation");
    EXPECT_EQ(status.message(), "relation 'relation_1' is self-referential for block 'doc_page_1_block_1'");
    EXPECT_FALSE(status.retryable());
    EXPECT_FALSE(std::filesystem::exists(output_path));
}

TEST(JsonDocumentExporterTest, ReturnsSerializationDiagnosticForInvalidUtf8) {
    const auto output_path = tempManifestPath("tdp_json_document_exporter_invalid_utf8_test.json");
    std::filesystem::remove(output_path);
    DocumentFixture fixture = makeDocumentFixture();
    fixture.document.blocks[0].text = std::string(1, static_cast<char>(0xff));

    const doc_parser::common::Status status = JsonDocumentExporter().write({
        false,
        output_path,
        &fixture.document,
        &fixture.artifacts,
    });

    EXPECT_FALSE(status.okStatus());
    EXPECT_EQ(status.stage(), "export");
    EXPECT_EQ(status.code(), "export.json.serialization_failed");
    EXPECT_NE(status.message().find("failed to serialize JSON document"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(output_path));
}

TEST(DocumentExporterTest, DefaultExporterPreservesJsonDiagnostic) {
    const auto exporter = doc_parser::exporter::createDefaultDocumentExporter();
    ASSERT_NE(exporter, nullptr);

    const doc_parser::common::Status status = exporter->write({
        false,
        tempManifestPath("tdp_default_exporter_missing_document_test.json"),
        nullptr,
    });

    EXPECT_FALSE(status.okStatus());
    EXPECT_EQ(status.stage(), "export");
    EXPECT_EQ(status.code(), "export.json.document_missing");
    EXPECT_EQ(status.message(), "document export request has no document");
}
