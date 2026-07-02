#include "document/text_model.h"
#include "pdf/pdf_document.h"
#include "pdf/text_service.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: pdf_text_extractor_smoke <fixture.pdf>\n";
        return 1;
    }

    doc_parser::pdf::PdfDocument document;
    if (!document.open(argv[1])) {
        std::cerr << "failed to open fixture PDF: " << argv[1] << '\n';
        return 1;
    }

    doc_parser::pdf::TextService text_service;
    std::vector<doc_parser::document::PageText> page_texts;
    if (!text_service.extractText(document, 72, page_texts) || page_texts.empty()) {
        std::cerr << "failed to extract document text\n";
        return 1;
    }

    const auto& page_text = page_texts.front();
    if (!page_text.has_text) {
        std::cerr << "expected fixture PDF to contain text\n";
        return 1;
    }
    if (page_text.preferred_source != doc_parser::document::TextSource::PdfTextLayer) {
        std::cerr << "unexpected preferred text source\n";
        return 1;
    }
    if (page_text.lines.empty() || page_text.lines.front().spans.empty()) {
        std::cerr << "expected extracted lines and spans\n";
        return 1;
    }

    const auto& span = page_text.lines.front().spans.front();
    if (span.text.empty()) {
        std::cerr << "expected non-empty extracted span text\n";
        return 1;
    }
    if (span.bbox.x1 < span.bbox.x0 || span.bbox.y1 < span.bbox.y0) {
        std::cerr << "expected valid span bbox\n";
        return 1;
    }

    return 0;
}
