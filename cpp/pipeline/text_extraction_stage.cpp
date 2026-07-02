#include "pipeline/text_extraction_stage.h"

namespace doc_parser::pipeline {

bool TextExtractionStage::extract(const pdf::PdfDocument& source,
                                  const std::vector<document::PageArtifact>& pages,
                                  int dpi,
                                  std::vector<document::PageText>& page_texts) const {
    page_texts.clear();

    if (!source.isOpen() || dpi <= 0) {
        return false;
    }

    if (!text_.extractText(source, dpi, page_texts)) {
        return false;
    }

    if (page_texts.size() != pages.size()) {
        return false;
    }

    for (std::size_t index = 0; index < page_texts.size(); ++index) {
        if (page_texts[index].has_text) {
            continue;
        }
        if (!ocr_.recognize(pages[index], dpi, page_texts[index])) {
            return false;
        }
    }

    return true;
}

} // namespace doc_parser::pipeline
