#include "export/json_manifest_writer.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

using doc_parser::document::BBox;
using doc_parser::document::DebugImageArtifact;
using doc_parser::document::PageArtifact;
using doc_parser::document::PageText;
using doc_parser::document::ParsedDocument;
using doc_parser::document::ParsedPage;
using doc_parser::document::TextLine;
using doc_parser::document::TextSource;
using doc_parser::document::TextSpan;
using doc_parser::exporter::JsonManifestWriter;

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
    image.debug_images.push_back(DebugImageArtifact{
        "preprocessed",
        "debug/page_1_preprocessed.png",
        "/tmp/debug/page_1_preprocessed.png",
    });

    ParsedDocument document;
    document.source.path = "fixture.pdf";
    document.source.type = "pdf";
    document.dpi = 144;
    document.pages.push_back(ParsedPage{
        0,
        1,
        image,
        text,
    });
    return document;
}

nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    return nlohmann::json::parse(input);
}

} // namespace

TEST(JsonManifestWriterTest, WritesManifestWithoutDebugFieldsByDefault) {
    const auto output_path = tempManifestPath("tdp_json_manifest_writer_normal_test.json");
    std::filesystem::remove(output_path);

    const ParsedDocument document = makeDocument();
    ASSERT_TRUE(JsonManifestWriter().write({
        false,
        output_path,
        &document,
    }));

    const auto manifest = readJson(output_path);
    EXPECT_EQ(manifest["source"]["path"], "fixture.pdf");
    EXPECT_EQ(manifest["source"]["type"], "pdf");
    EXPECT_EQ(manifest["render"]["dpi"], 144);
    ASSERT_EQ(manifest["pages"].size(), 1U);
    EXPECT_EQ(manifest["pages"][0]["page_index"], 0);
    EXPECT_EQ(manifest["pages"][0]["page_number"], 1);
    EXPECT_EQ(manifest["pages"][0]["image"], "pages/page_1.png");
    EXPECT_FALSE(manifest["pages"][0].contains("debug"));

    std::filesystem::remove(output_path);
}

TEST(JsonManifestWriterTest, WritesDebugTextAndImagesWhenRequested) {
    const auto output_path = tempManifestPath("tdp_json_manifest_writer_debug_test.json");
    std::filesystem::remove(output_path);

    const ParsedDocument document = makeDocument();
    ASSERT_TRUE(JsonManifestWriter().write({
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
    ASSERT_EQ(debug["images"].size(), 1U);
    EXPECT_EQ(debug["images"][0]["name"], "preprocessed");
    EXPECT_EQ(debug["images"][0]["image"], "debug/page_1_preprocessed.png");

    std::filesystem::remove(output_path);
}

TEST(JsonManifestWriterTest, RejectsMissingDocument) {
    const auto output_path = tempManifestPath("tdp_json_manifest_writer_missing_document_test.json");

    EXPECT_FALSE(JsonManifestWriter().write({
        false,
        output_path,
        nullptr,
    }));
}
