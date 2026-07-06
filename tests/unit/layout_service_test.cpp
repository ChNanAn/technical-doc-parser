#include "layout/layout_service.h"

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

doc_parser::document::TextLine makeLine(const std::string& text, doc_parser::document::BBox bbox) {
    doc_parser::document::TextSpan span;
    span.text = text;
    span.bbox = bbox;
    span.source = doc_parser::document::TextSource::Ocr;
    span.confidence = 0.9;

    doc_parser::document::TextLine line;
    line.text = text;
    line.bbox = bbox;
    line.source = doc_parser::document::TextSource::Ocr;
    line.confidence = 0.9;
    line.spans.push_back(span);
    return line;
}

} // namespace

TEST(LayoutServiceTest, BuildsTitleAndTextBlocksFromPageText) {
    doc_parser::document::PageText text;
    text.page_index = 0;
    text.page_number = 1;
    text.has_text = true;
    text.preferred_source = doc_parser::document::TextSource::Ocr;
    text.lines.push_back(makeLine("Technical Manual", {120.0, 150.0, 460.0, 190.0}));
    text.lines.push_back(makeLine("This product supports OCR layout analysis.", {120.0, 240.0, 760.0, 270.0}));
    text.lines.push_back(makeLine("It normalizes blocks for downstream stages.", {120.0, 280.0, 730.0, 310.0}));

    const doc_parser::layout::LayoutService service;
    doc_parser::document::PageLayout layout;
    ASSERT_TRUE(service.analyze(makePage(), text, layout));

    ASSERT_EQ(layout.blocks.size(), 2U);
    EXPECT_EQ(layout.blocks[0].type, doc_parser::document::LayoutBlockType::Title);
    EXPECT_EQ(layout.blocks[0].text_line_indices[0], 0);
    EXPECT_EQ(layout.blocks[1].type, doc_parser::document::LayoutBlockType::Text);
    ASSERT_EQ(layout.blocks[1].text_line_indices.size(), 2U);
    EXPECT_EQ(layout.blocks[1].text_line_indices[0], 1);
    EXPECT_EQ(layout.blocks[1].text_line_indices[1], 2);
}

TEST(LayoutServiceTest, EmitsFigureBlockWhenPageHasNoText) {
    const doc_parser::layout::LayoutService service;
    doc_parser::document::PageText text;
    text.page_index = 0;
    text.page_number = 1;

    doc_parser::document::PageLayout layout;
    ASSERT_TRUE(service.analyze(makePage(), text, layout));

    ASSERT_EQ(layout.blocks.size(), 1U);
    EXPECT_EQ(layout.blocks[0].type, doc_parser::document::LayoutBlockType::Figure);
    EXPECT_EQ(layout.blocks[0].bbox.x1, 1000.0);
    EXPECT_EQ(layout.blocks[0].bbox.y1, 1400.0);
}
