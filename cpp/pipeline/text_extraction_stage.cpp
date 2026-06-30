#include "pipeline/text_extraction_stage.h"

#include "pdf/pdf_text_extractor.h"

#include <iostream>

namespace doc_parser::pipeline {

bool TextExtractionStage::extract(
    const TextExtractionInput& input,
    std::vector<document::PageText>& page_texts
) const {
    page_texts.clear();

    if (input.reader == nullptr || input.pages == nullptr || input.dpi <= 0) {
        return false;
    }

    pdf::PdfTextExtractor pdf_text_extractor;
    page_texts.reserve(input.pages->size());

    for (const auto& page : *input.pages) {
        document::PageText page_text;
        if (!pdf_text_extractor.extractPageText(
                *input.reader,
                {
                    page.page_index,
                    input.dpi,
                },
                page_text
            )) {
            std::cerr << "error: failed to extract text for page " << page.page_number << '\n';
            return false;
        }

        page_texts.push_back(page_text);
    }

    return true;
}

}  // namespace doc_parser::pipeline
