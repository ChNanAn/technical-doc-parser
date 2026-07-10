#include "document_source/pdf/text_service.h"

#include "document_source/pdf/pdfium/pdf_text_extractor.h"

#include <iostream>

namespace doc_parser::pdf {

bool TextService::extractText(const PdfDocument& source, int dpi, std::vector<document::PageText>& page_texts) const {
    page_texts.clear();

    if (!source.isOpen() || dpi <= 0) {
        return false;
    }

    const int page_count = source.pageCount();
    if (page_count <= 0) {
        return true;
    }

    PdfTextExtractor pdf_text_extractor;
    page_texts.reserve(static_cast<std::size_t>(page_count));

    for (int page_index = 0; page_index < page_count; ++page_index) {
        document::PageText page_text;
        if (!pdf_text_extractor.extractPageText(source.reader(), {page_index, dpi}, page_text)) {
            std::cerr << "error: failed to extract text for page " << (page_index + 1) << '\n';
            return false;
        }
        if (!page_text.has_text) {
            page_text.preferred_source = document::TextSource::Unknown;
            // TODO(ocr): the pipeline stage will dispatch empty pages to OCR.
        }
        page_texts.push_back(std::move(page_text));
    }

    return true;
}

} // namespace doc_parser::pdf
