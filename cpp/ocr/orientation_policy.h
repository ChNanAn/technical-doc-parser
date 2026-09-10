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

// At least two crops and a two-thirds majority must share the proposed axis.
bool hasDominantCropRotation(const std::vector<int>& clockwise_degrees, int degrees);

// Compare identical tall crops at source, clockwise 90 and clockwise 270 poses.
// The same lines must beat both the source pose and the opposing direction by a clear margin.
int supportedQuarterTurn(const std::vector<RecognitionEvidence>& source,
                         const std::vector<RecognitionEvidence>& clockwise_90,
                         const std::vector<RecognitionEvidence>& clockwise_270);

} // namespace doc_parser::ocr
