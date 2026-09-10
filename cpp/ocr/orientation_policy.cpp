#include "ocr/orientation_policy.h"

#include <algorithm>
#include <cmath>

namespace doc_parser::ocr {
namespace {

double score(const RecognitionEvidence& line) {
    return line.characters >= 4 && std::isfinite(line.confidence) ? std::clamp(line.confidence, 0.0, 1.0) : 0.0;
}

} // namespace

double recognitionEvidenceScore(const std::vector<RecognitionEvidence>& lines) {
    if (lines.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (const auto& line : lines) {
        total += score(line);
    }
    return total / static_cast<double>(lines.size());
}

bool shouldProbeUpsideDown(const std::vector<RecognitionEvidence>& lines) {
    return lines.size() >= 2 && recognitionEvidenceScore(lines) < 0.8;
}

bool supportsUpsideDown(const std::vector<RecognitionEvidence>& original,
                        const std::vector<RecognitionEvidence>& rotated) {
    if (original.size() != rotated.size() || rotated.size() < 2) {
        return false;
    }
    std::size_t votes = 0;
    for (std::size_t index = 0; index < rotated.size(); ++index) {
        if (score(rotated[index]) >= 0.85 && score(rotated[index]) - score(original[index]) >= 0.15) {
            ++votes;
        }
    }
    return votes >= 2 && votes * 3 >= rotated.size() * 2 &&
           recognitionEvidenceScore(rotated) - recognitionEvidenceScore(original) >= 0.15;
}

} // namespace doc_parser::ocr
