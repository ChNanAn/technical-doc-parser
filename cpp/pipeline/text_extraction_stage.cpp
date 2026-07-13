#include "pipeline/text_extraction_stage.h"

#include <string>

namespace doc_parser::pipeline {

TextExtractionStage::TextExtractionStage(const document_source::INativeTextExtractor* native_text_extractor,
                                         const ocr::IOcrBackend& ocr)
    : native_text_extractor_(native_text_extractor), ocr_(ocr) {}

common::Status TextExtractionStage::extract(const PipelineContext& context,
                                            const std::vector<document::PageArtifact>& pages,
                                            std::vector<document::PageText>& page_texts) const {
    page_texts.clear();

    if (context.render.dpi <= 0) {
        return common::Status::error("text.invalid_dpi", "render DPI must be positive");
    }

    if (native_text_extractor_ != nullptr) {
        if (!native_text_extractor_->extractNativeText({context.render.dpi}, page_texts)) {
            return common::Status::error("text.native_extraction_failed", "native text extraction failed");
        }
    } else {
        page_texts.reserve(pages.size());
        for (const auto& page : pages) {
            document::PageText page_text;
            page_text.page_index = page.page_index;
            page_text.page_number = page.page_number;
            page_text.preferred_source = document::TextSource::Unknown;
            page_texts.push_back(page_text);
        }
    }

    if (page_texts.size() != pages.size()) {
        return common::Status::error("text.page_count_mismatch",
                                     "native text page count does not match page artifacts");
    }

    for (std::size_t index = 0; index < page_texts.size(); ++index) {
        if (page_texts[index].has_text) {
            continue;
        }
        ocr::OcrResult result;
        if (!ocr_.recognize({pages[index], context.render.dpi}, result)) {
            return common::Status::error("text.ocr_failed", "OCR failed for page " + std::to_string(index + 1));
        }
        page_texts[index] = result.page_text;
    }

    return common::Status::ok();
}

} // namespace doc_parser::pipeline
