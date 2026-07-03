#include "document/text_normalizer.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using doc_parser::document::BBox;
using doc_parser::document::TextNormalizer;
using doc_parser::document::TextSource;
using doc_parser::document::TextToken;
using doc_parser::document::TextTokenKind;

TextToken glyph(const std::string& text, double x0, double x1) {
    return {
        TextTokenKind::Glyph,
        text,
        BBox{x0, 0.0, x1, 1.0},
        TextSource::PdfTextLayer,
        1.0,
    };
}

TextToken lineBreak() {
    TextToken token;
    token.kind = TextTokenKind::LineBreak;
    return token;
}

} // namespace

TEST(TextNormalizerTest, GroupsGlyphsIntoWordSpans) {
    const std::vector<TextToken> tokens = {
        glyph("T", 0.0, 1.0),
        glyph("a", 1.0, 2.0),
        glyph("b", 2.0, 3.0),
        glyph("l", 3.0, 4.0),
        glyph("e", 4.0, 5.0),
        glyph(" ", 5.0, 6.0),
        glyph("O", 6.0, 7.0),
        glyph("f", 7.0, 8.0),
        glyph(" ", 8.0, 9.0),
        glyph("C", 9.0, 10.0),
        glyph("o", 10.0, 11.0),
        glyph("n", 11.0, 12.0),
        glyph("t", 12.0, 13.0),
        glyph("e", 13.0, 14.0),
        glyph("n", 14.0, 15.0),
        glyph("t", 15.0, 16.0),
    };

    const auto page_text = TextNormalizer().normalize(0, tokens);

    ASSERT_TRUE(page_text.has_text);
    EXPECT_EQ(page_text.page_index, 0);
    EXPECT_EQ(page_text.page_number, 1);
    EXPECT_EQ(page_text.preferred_source, TextSource::PdfTextLayer);
    ASSERT_EQ(page_text.lines.size(), 1U);

    const auto& line = page_text.lines.front();
    EXPECT_EQ(line.text, "Table Of Content");
    EXPECT_DOUBLE_EQ(line.bbox.x0, 0.0);
    EXPECT_DOUBLE_EQ(line.bbox.x1, 16.0);
    ASSERT_EQ(line.spans.size(), 3U);
    EXPECT_EQ(line.spans[0].text, "Table");
    EXPECT_EQ(line.spans[1].text, "Of");
    EXPECT_EQ(line.spans[2].text, "Content");
}

TEST(TextNormalizerTest, SplitsLinesOnLineBreakTokens) {
    const std::vector<TextToken> tokens = {
        glyph("A", 0.0, 1.0),
        lineBreak(),
        glyph(" ", 0.0, 1.0),
        glyph("B", 0.0, 1.0),
    };

    const auto page_text = TextNormalizer().normalize(2, tokens);

    ASSERT_TRUE(page_text.has_text);
    EXPECT_EQ(page_text.page_index, 2);
    EXPECT_EQ(page_text.page_number, 3);
    ASSERT_EQ(page_text.lines.size(), 2U);
    EXPECT_EQ(page_text.lines[0].text, "A");
    EXPECT_EQ(page_text.lines[1].text, "B");
}

TEST(TextNormalizerTest, IgnoresWhitespaceOnlyInput) {
    const std::vector<TextToken> tokens = {
        glyph(" ", 0.0, 1.0),
        glyph("\t", 1.0, 2.0),
        lineBreak(),
        glyph(" ", 0.0, 1.0),
    };

    const auto page_text = TextNormalizer().normalize(0, tokens);

    EXPECT_FALSE(page_text.has_text);
    EXPECT_EQ(page_text.preferred_source, TextSource::Unknown);
    EXPECT_TRUE(page_text.lines.empty());
}
