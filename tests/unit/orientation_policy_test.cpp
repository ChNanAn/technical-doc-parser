#include "ocr/orientation_policy.h"

#include <gtest/gtest.h>

#include <limits>

namespace {

using doc_parser::ocr::hasDominantCropRotation;
using doc_parser::ocr::recognitionEvidenceScore;
using doc_parser::ocr::shouldProbeUpsideDown;
using doc_parser::ocr::supportedQuarterTurn;
using doc_parser::ocr::supportsUpsideDown;

TEST(OrientationPolicyTest, ConfidentUprightAndSparsePagesAvoidProbes) {
    EXPECT_FALSE(shouldProbeUpsideDown({}));
    EXPECT_FALSE(shouldProbeUpsideDown({{0.2, 20}}));
    EXPECT_FALSE(shouldProbeUpsideDown({{0.98, 20}, {0.92, 12}, {0.96, 16}}));
    EXPECT_TRUE(shouldProbeUpsideDown({{0.3, 20}, {0.4, 12}}));
}

TEST(OrientationPolicyTest, ConsistentMultiLineEvidenceAllowsRecovery) {
    EXPECT_TRUE(supportsUpsideDown({{0.3, 20}, {0.4, 12}, {0.95, 8}}, {{0.96, 22}, {0.98, 14}, {0.94, 8}}));
}

TEST(OrientationPolicyTest, WeakMixedOrMismatchedEvidencePreservesOrientation) {
    EXPECT_FALSE(supportsUpsideDown({{0.4, 20}}, {{0.99, 24}}));
    EXPECT_FALSE(supportsUpsideDown({{0.4, 20}, {0.4, 20}}, {{0.99, 24}}));
    EXPECT_FALSE(supportsUpsideDown({{0.9, 20}, {0.9, 20}}, {{0.99, 20}, {0.99, 20}}));
    EXPECT_FALSE(supportsUpsideDown({{0.2, 20}, {0.9, 20}, {0.9, 20}}, {{0.99, 20}, {0.2, 20}, {0.2, 20}}));
}

TEST(OrientationPolicyTest, BlankShortAndNonFiniteDecodesCannotWin) {
    EXPECT_DOUBLE_EQ(recognitionEvidenceScore({{0.99, 0}, {0.99, 1}, {std::numeric_limits<double>::quiet_NaN(), 20}}),
                     0.0);
    EXPECT_FALSE(supportsUpsideDown({{0.2, 20}, {0.2, 20}}, {{0.99, 1}, {0.99, 0}}));
}

TEST(OrientationPolicyTest, QuarterTurnRequiresMultipleCropsSharingAnAxis) {
    EXPECT_FALSE(hasDominantCropRotation({}, 90));
    EXPECT_FALSE(hasDominantCropRotation({90}, 90));
    EXPECT_FALSE(hasDominantCropRotation({0, 90, 0, 90}, 90));
    EXPECT_FALSE(hasDominantCropRotation({0, 90, 0, 90}, 0));
    EXPECT_TRUE(hasDominantCropRotation({90, 0, 90}, 90));
    EXPECT_TRUE(hasDominantCropRotation({0, 0, 90}, 0));
}

TEST(OrientationPolicyTest, QuarterTurnUsesAbsoluteCropPoseEvenWhenFirstRecognitionIsConfident) {
    const std::vector<doc_parser::ocr::RecognitionEvidence> weak{{0.3, 12}, {0.2, 18}, {0.4, 15}};
    const std::vector<doc_parser::ocr::RecognitionEvidence> strong{{0.98, 12}, {0.97, 18}, {0.96, 15}};
    EXPECT_EQ(supportedQuarterTurn(weak, strong, weak), 90);
    EXPECT_EQ(supportedQuarterTurn(weak, weak, strong), 270);
    // Vertical writing readable in the original pose, ties, and inconsistent votes cannot rotate a page.
    EXPECT_EQ(supportedQuarterTurn(strong, strong, weak), 0);
    EXPECT_EQ(supportedQuarterTurn(weak, strong, strong), 0);
    EXPECT_EQ(supportedQuarterTurn(weak, {{0.99, 12}, {0.3, 18}, {0.99, 15}}, {{0.3, 12}, {0.99, 18}, {0.99, 15}}), 0);
}

TEST(OrientationPolicyTest, QuarterTurnRejectsSparseInvalidAndMismatchedEvidence) {
    EXPECT_EQ(supportedQuarterTurn({{0.2, 15}}, {{0.99, 15}}, {{0.2, 15}}), 0);
    EXPECT_EQ(supportedQuarterTurn({{0.2, 15}, {0.2, 15}}, {{0.99, 15}}, {{0.2, 15}, {0.2, 15}}), 0);
    EXPECT_EQ(supportedQuarterTurn({{0.2, 15}, {0.2, 15}},
                                   {{0.99, 1}, {std::numeric_limits<double>::infinity(), 15}},
                                   {{0.99, 0}, {0.99, 2}}),
              0);
}

TEST(OrientationPolicyTest, QuarterTurnMustBeatBothAlternativesOnTheSameLines) {
    // Separate two-thirds majorities overlap on only one line, which is insufficient evidence.
    EXPECT_EQ(supportedQuarterTurn({{0.98, 12}, {0.2, 18}, {0.2, 15}},
                                   {{0.99, 12}, {0.99, 18}, {0.99, 15}},
                                   {{0.2, 12}, {0.98, 18}, {0.2, 15}}),
              0);
}

} // namespace
