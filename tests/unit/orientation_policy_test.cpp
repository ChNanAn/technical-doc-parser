#include "ocr/orientation_policy.h"

#include <gtest/gtest.h>

#include <limits>

namespace {

using doc_parser::ocr::recognitionEvidenceScore;
using doc_parser::ocr::shouldProbeUpsideDown;
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

} // namespace
