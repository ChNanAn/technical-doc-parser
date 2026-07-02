#include "pipeline/text_extraction_stage.h"

namespace doc_parser::pipeline {

bool TextExtractionStage::extract(const pdf::PdfReader& source,
                                  int dpi,
                                  std::vector<document::PageText>& page_texts) const {
    page_texts.clear();

    if (!source.isOpen() || dpi <= 0) {
        return false;
    }

    if (!text_.extractText(source, dpi, page_texts)) {
        return false;
    }

    // TODO(ocr): for pages where page_texts[i].has_text is false, dispatch
    // to an OCR adapter and update the entry in place.

    return true;
}

} // namespace doc_parser::pipeline
