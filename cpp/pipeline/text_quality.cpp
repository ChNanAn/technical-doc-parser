#include "pipeline/text_quality.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace doc_parser::pipeline {
namespace {

double bboxArea(const document::BBox& bbox) {
    return std::max(0.0, bbox.x1 - bbox.x0) * std::max(0.0, bbox.y1 - bbox.y0);
}

double overlapOverSmallerArea(const document::BBox& lhs, const document::BBox& rhs) {
    const double width = std::max(0.0, std::min(lhs.x1, rhs.x1) - std::max(lhs.x0, rhs.x0));
    const double height = std::max(0.0, std::min(lhs.y1, rhs.y1) - std::max(lhs.y0, rhs.y0));
    const double denominator = std::min(bboxArea(lhs), bboxArea(rhs));
    return denominator <= 0.0 ? 0.0 : width * height / denominator;
}

double verticalCoverage(const document::PageArtifact& page, const document::PageText& text) {
    if (page.height <= 0) {
        return 0.0;
    }

    std::vector<std::pair<double, double>> intervals;
    for (const document::TextLine& line : text.lines) {
        const double y0 = std::clamp(line.bbox.y0, 0.0, static_cast<double>(page.height));
        const double y1 = std::clamp(line.bbox.y1, 0.0, static_cast<double>(page.height));
        if (y1 > y0) {
            intervals.emplace_back(y0, y1);
        }
    }
    if (intervals.empty()) {
        return 0.0;
    }

    std::sort(intervals.begin(), intervals.end());
    double covered = 0.0;
    double current_start = intervals.front().first;
    double current_end = intervals.front().second;
    for (std::size_t index = 1; index < intervals.size(); ++index) {
        if (intervals[index].first <= current_end) {
            current_end = std::max(current_end, intervals[index].second);
        } else {
            covered += current_end - current_start;
            current_start = intervals[index].first;
            current_end = intervals[index].second;
        }
    }
    covered += current_end - current_start;
    return covered / static_cast<double>(page.height);
}

bool isDuplicateLine(const document::TextLine& candidate,
                     const std::vector<document::TextLine>& existing,
                     double threshold) {
    return std::any_of(existing.begin(), existing.end(), [&](const document::TextLine& line) {
        return overlapOverSmallerArea(candidate.bbox, line.bbox) >= threshold;
    });
}

} // namespace

NativeTextQualityPolicy::NativeTextQualityPolicy(NativeTextQualityConfig config) : config_(std::move(config)) {}

NativeTextQuality NativeTextQualityPolicy::evaluate(const document::PageArtifact& page,
                                                    const document::PageText& native_text) const {
    NativeTextQuality quality;
    for (const document::TextLine& line : native_text.lines) {
        for (const unsigned char value : line.text) {
            if (!std::isspace(value)) {
                ++quality.non_whitespace_bytes;
            }
            if ((value < 0x20U && !std::isspace(value)) || value == 0x7FU) {
                ++quality.suspicious_bytes;
            }
        }
        std::size_t replacement = line.text.find("\xEF\xBF\xBD");
        while (replacement != std::string::npos) {
            quality.suspicious_bytes += 3;
            replacement = line.text.find("\xEF\xBF\xBD", replacement + 3);
        }
    }
    quality.vertical_coverage = verticalCoverage(page, native_text);

    if (!native_text.has_text || quality.non_whitespace_bytes == 0) {
        quality.action = NativeTextAction::UseOcr;
        quality.reason = "empty_native_text";
        return quality;
    }

    const double suspicious_ratio =
        static_cast<double>(quality.suspicious_bytes) / static_cast<double>(quality.non_whitespace_bytes);
    if (suspicious_ratio > config_.suspicious_byte_ratio_threshold) {
        quality.action = NativeTextAction::UseOcr;
        quality.reason = "suspicious_native_text";
        return quality;
    }

    if (page.height > 0 && quality.non_whitespace_bytes < config_.sparse_text_byte_threshold &&
        quality.vertical_coverage < config_.sparse_vertical_coverage_threshold) {
        quality.action = NativeTextAction::MergeOcr;
        quality.reason = "sparse_native_text";
        return quality;
    }

    quality.action = NativeTextAction::UseNative;
    quality.reason = "usable_native_text";
    return quality;
}

TextMergeResult NativeTextQualityPolicy::merge(const document::PageText& native_text,
                                               const document::PageText& ocr_text) const {
    TextMergeResult result;
    result.text = native_text;
    for (const document::TextLine& line : ocr_text.lines) {
        if (line.text.empty() || isDuplicateLine(line, result.text.lines, config_.duplicate_overlap_threshold)) {
            continue;
        }
        result.text.lines.push_back(line);
        ++result.added_ocr_lines;
    }

    std::stable_sort(result.text.lines.begin(), result.text.lines.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.bbox.y0 != rhs.bbox.y0) {
            return lhs.bbox.y0 < rhs.bbox.y0;
        }
        return lhs.bbox.x0 < rhs.bbox.x0;
    });
    result.text.has_text = !result.text.lines.empty();
    if (result.added_ocr_lines > 0) {
        result.text.preferred_source = document::TextSource::Mixed;
    }
    return result;
}

const char* nativeTextActionName(NativeTextAction action) {
    switch (action) {
    case NativeTextAction::UseNative:
        return "use_native";
    case NativeTextAction::UseOcr:
        return "use_ocr";
    case NativeTextAction::MergeOcr:
        return "merge_ocr";
    }
    return "unknown";
}

} // namespace doc_parser::pipeline
