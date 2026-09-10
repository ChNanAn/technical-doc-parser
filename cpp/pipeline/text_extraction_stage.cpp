#include "pipeline/text_extraction_stage.h"

#include "common/warning_codes.h"

#include "pipeline/text_quality.h"

#include <spdlog/spdlog.h>
#include <string>
#include <utility>

namespace doc_parser::pipeline {

TextExtractionStage::TextExtractionStage(const document_source::INativeTextExtractor* native_text_extractor,
                                         const ocr::IOcrBackend& ocr)
    : native_text_extractor_(native_text_extractor), ocr_(ocr) {}

StageResult<std::vector<document::PageText>>
TextExtractionStage::extract(const PipelineContext& context, const std::vector<document::PageArtifact>& pages) const {
    StageResult<std::vector<document::PageText>> extraction;
    std::vector<document::PageText>& page_texts = extraction.value;

    if (context.render.dpi <= 0) {
        extraction.status = common::Status::error("text.invalid_dpi", "render DPI must be positive");
        return extraction;
    }

    if (native_text_extractor_ != nullptr) {
        if (!native_text_extractor_->extractNativeText({context.render.dpi}, page_texts)) {
            extraction.status = common::Status::error("text.native_extraction_failed", "native text extraction failed");
            return extraction;
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
        extraction.status =
            common::Status::error("text.page_count_mismatch", "native text page count does not match page artifacts");
        return extraction;
    }

    for (std::size_t index = 0; index < page_texts.size(); ++index) {
        auto result = extractPage(context, pages[index], std::move(page_texts[index]));
        page_texts[index] = std::move(result.value);
        if (!result.ok()) {
            extraction.status = result.status;
            return extraction;
        }
        extraction.diagnostics.insert(
            extraction.diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
    }
    return extraction;
}

PageTextExtractionResult TextExtractionStage::extractPage(const PipelineContext& context,
                                                          const document::PageArtifact& page,
                                                          document::PageText native_text) const {
    PageTextExtractionResult extraction;
    extraction.value = std::move(native_text);
    if (context.render.dpi <= 0) {
        extraction.status = common::Status::error("text.invalid_dpi", "render DPI must be positive");
        return extraction;
    }
    const NativeTextQualityPolicy quality_policy;
    const NativeTextQuality quality = quality_policy.evaluate(page, extraction.value);
    spdlog::debug("text_quality: page={} action={} reason={} bytes={} suspicious={} decoded={} controls={} "
                  "control_types={} damaging_controls={} damaging_control_types={} damaging_ratio={:.3f} "
                  "invalid_utf16={} replacements={} vertical_coverage={:.3f}",
                  page.page_number,
                  nativeTextActionName(quality.action),
                  quality.reason,
                  quality.non_whitespace_bytes,
                  quality.suspicious_bytes,
                  extraction.value.extraction_signals.decoded_codepoints,
                  quality.control_codepoints,
                  quality.distinct_control_codepoints,
                  quality.damaging_control_codepoints,
                  quality.distinct_damaging_control_codepoints,
                  quality.damaging_control_ratio,
                  extraction.value.extraction_signals.invalid_utf16_codepoints,
                  extraction.value.extraction_signals.replacement_codepoints,
                  quality.vertical_coverage);
    for (std::size_t codepoint = 0; codepoint < extraction.value.extraction_signals.c0_control_counts.size();
         ++codepoint) {
        const std::size_t count = extraction.value.extraction_signals.c0_control_counts[codepoint];
        if (count > 0) {
            spdlog::debug("text_quality: page={} control=U+{:04X} count={}", page.page_number, codepoint, count);
        }
    }
    if (quality.action == NativeTextAction::UseNative) {
        return extraction;
    }

    ocr::OcrResult result;
    if (!ocr_.recognize({page, context.render.dpi}, result)) {
        if (quality.action == NativeTextAction::MergeOcr) {
            const std::string message = "OCR enhancement failed; retained usable native text";
            spdlog::warn("text_quality: {} for page {}", message, page.page_number);
            extraction.diagnostics.push_back({
                common::warning_codes::kOcrEnhancementFailed,
                message,
                "text",
                page.page_number,
                {{"fallback", "native_text"}, {"reason", quality.reason}},
            });
            return extraction;
        }
        const std::string unavailable_reason = ocr_.unavailableReason();
        const std::string message =
            unavailable_reason.empty()
                ? "OCR failed for page " + std::to_string(page.page_number)
                : "OCR is required for page " + std::to_string(page.page_number) + ": " + unavailable_reason;
        extraction.status = common::Status::error("text.ocr_failed", message);
        return extraction;
    }
    extraction.clockwise_correction_degrees = result.clockwise_correction_degrees;
    if (result.clockwise_correction_degrees != 0) {
        spdlog::debug("ocr_orientation: page={} correction={} coordinates=source",
                      page.page_number,
                      result.clockwise_correction_degrees);
    }
    if (quality.action == NativeTextAction::MergeOcr) {
        TextMergeResult merged = quality_policy.merge(extraction.value, result.page_text);
        spdlog::debug("text_quality: page={} merged_ocr_lines={}", page.page_number, merged.added_ocr_lines);
        extraction.value = std::move(merged.text);
    } else {
        extraction.value = std::move(result.page_text);
    }

    return extraction;
}

} // namespace doc_parser::pipeline
