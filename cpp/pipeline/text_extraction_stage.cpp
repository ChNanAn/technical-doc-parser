#include "pipeline/text_extraction_stage.h"

namespace doc_parser::pipeline {

TextExtractionStage::TextExtractionStage(const IDocumentBackend& document_backend, const ocr::OcrService& ocr)
    : document_backend_(document_backend), ocr_(ocr) {}

bool TextExtractionStage::extract(const PipelineContext& context,
                                  const std::vector<document::PageArtifact>& pages,
                                  std::vector<document::PageText>& page_texts) const {
    page_texts.clear();

    if (context.render.dpi <= 0) {
        return false;
    }

    if (!document_backend_.extractNativeText(context, page_texts)) {
        return false;
    }

    if (page_texts.size() != pages.size()) {
        return false;
    }

    for (std::size_t index = 0; index < page_texts.size(); ++index) {
        if (page_texts[index].has_text) {
            continue;
        }
        if (!ocr_.recognize(pages[index], context.render.dpi, page_texts[index])) {
            return false;
        }
    }

    return true;
}

} // namespace doc_parser::pipeline
