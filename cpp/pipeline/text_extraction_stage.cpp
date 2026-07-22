#include "pipeline/text_extraction_stage.h"

#include "pipeline/text_quality.h"

#include <spdlog/spdlog.h>
#include <string>
#include <utility>

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

    const NativeTextQualityPolicy quality_policy;
    for (std::size_t index = 0; index < page_texts.size(); ++index) {
        const NativeTextQuality quality = quality_policy.evaluate(pages[index], page_texts[index]);
        spdlog::debug("text_quality: page={} action={} reason={} bytes={} suspicious={} vertical_coverage={:.3f}",
                      pages[index].page_number,
                      nativeTextActionName(quality.action),
                      quality.reason,
                      quality.non_whitespace_bytes,
                      quality.suspicious_bytes,
                      quality.vertical_coverage);
        if (quality.action == NativeTextAction::UseNative) {
            continue;
        }

        ocr::OcrResult result;
        if (!ocr_.recognize({pages[index], context.render.dpi}, result)) {
            if (quality.action == NativeTextAction::MergeOcr) {
                spdlog::warn("text_quality: OCR enhancement failed for page {}; keeping usable native text",
                             pages[index].page_number);
                continue;
            }
            const std::string unavailable_reason = ocr_.unavailableReason();
            const std::string message =
                unavailable_reason.empty()
                    ? "OCR failed for page " + std::to_string(index + 1)
                    : "OCR is required for page " + std::to_string(index + 1) + ": " + unavailable_reason;
            return common::Status::error("text.ocr_failed", message);
        }
        if (quality.action == NativeTextAction::MergeOcr) {
            TextMergeResult merged = quality_policy.merge(page_texts[index], result.page_text);
            spdlog::debug(
                "text_quality: page={} merged_ocr_lines={}", pages[index].page_number, merged.added_ocr_lines);
            page_texts[index] = std::move(merged.text);
        } else {
            page_texts[index] = std::move(result.page_text);
        }
    }

    return common::Status::ok();
}

} // namespace doc_parser::pipeline
