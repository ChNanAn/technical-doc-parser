#include "document/text_normalizer.h"

#include <iostream>
#include <vector>

int main() {
    using doc_parser::document::BBox;
    using doc_parser::document::TextNormalizer;
    using doc_parser::document::TextSource;
    using doc_parser::document::TextToken;
    using doc_parser::document::TextTokenKind;

    const std::vector<TextToken> tokens = {
        {TextTokenKind::Glyph, "T", BBox{0.0, 0.0, 1.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "a", BBox{1.0, 0.0, 2.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "b", BBox{2.0, 0.0, 3.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "l", BBox{3.0, 0.0, 4.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "e", BBox{4.0, 0.0, 5.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, " ", BBox{5.0, 0.0, 6.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "O", BBox{6.0, 0.0, 7.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "f", BBox{7.0, 0.0, 8.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, " ", BBox{8.0, 0.0, 9.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "C", BBox{9.0, 0.0, 10.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "o", BBox{10.0, 0.0, 11.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "n", BBox{11.0, 0.0, 12.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "t", BBox{12.0, 0.0, 13.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "e", BBox{13.0, 0.0, 14.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "n", BBox{14.0, 0.0, 15.0, 1.0}, TextSource::PdfTextLayer, 1.0},
        {TextTokenKind::Glyph, "t", BBox{15.0, 0.0, 16.0, 1.0}, TextSource::PdfTextLayer, 1.0},
    };

    const auto page_text = TextNormalizer().normalize(0, tokens);
    if (!page_text.has_text || page_text.lines.size() != 1) {
        std::cerr << "expected one text line\n";
        return 1;
    }

    const auto& line = page_text.lines.front();
    if (line.text != "Table Of Content") {
        std::cerr << "unexpected line text: " << line.text << '\n';
        return 1;
    }
    if (line.spans.size() != 3 || line.spans[0].text != "Table" || line.spans[1].text != "Of" ||
        line.spans[2].text != "Content") {
        std::cerr << "unexpected text spans\n";
        return 1;
    }

    return 0;
}
