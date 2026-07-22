#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"

#include <cstddef>
#include <string>

namespace doc_parser::pipeline {

enum class NativeTextAction {
    UseNative,
    UseOcr,
    MergeOcr,
};

struct NativeTextQuality {
    NativeTextAction action = NativeTextAction::UseOcr;
    std::size_t non_whitespace_bytes = 0;
    std::size_t suspicious_bytes = 0;
    double vertical_coverage = 0.0;
    std::string reason;
};

struct NativeTextQualityConfig {
    std::size_t sparse_text_byte_threshold = 256;
    double sparse_vertical_coverage_threshold = 0.12;
    double suspicious_byte_ratio_threshold = 0.02;
    double duplicate_overlap_threshold = 0.5;
};

struct TextMergeResult {
    document::PageText text;
    std::size_t added_ocr_lines = 0;
};

class NativeTextQualityPolicy {
public:
    explicit NativeTextQualityPolicy(NativeTextQualityConfig config = {});

    NativeTextQuality evaluate(const document::PageArtifact& page, const document::PageText& native_text) const;
    TextMergeResult merge(const document::PageText& native_text, const document::PageText& ocr_text) const;

private:
    NativeTextQualityConfig config_;
};

const char* nativeTextActionName(NativeTextAction action);

} // namespace doc_parser::pipeline
