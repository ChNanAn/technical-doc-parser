#pragma once

#include <cstddef>
#include <vector>

namespace doc_parser::ocr {

struct RecognitionEvidence {
    double confidence = 0.0;
    std::size_t characters = 0;
};

// Confidence is a recovery signal, not an accuracy estimate. Require consistent
// evidence from multiple lines; sparse/ambiguous pages keep their original pose.
double recognitionEvidenceScore(const std::vector<RecognitionEvidence>& lines);
bool shouldProbeUpsideDown(const std::vector<RecognitionEvidence>& lines);
bool supportsUpsideDown(const std::vector<RecognitionEvidence>& original,
                        const std::vector<RecognitionEvidence>& rotated);

} // namespace doc_parser::ocr
