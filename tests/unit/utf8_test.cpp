#include "common/utf8.h"

#include <gtest/gtest.h>

TEST(Utf8Test, EncodesUnicodeScalarValues) {
    EXPECT_EQ(doc_parser::common::encodeUtf8(0x41U), "A");
    EXPECT_EQ(doc_parser::common::encodeUtf8(0x2264U), "\xE2\x89\xA4");
    EXPECT_EQ(doc_parser::common::encodeUtf8(0x1D437U), "\xF0\x9D\x90\xB7");
}

TEST(Utf8Test, CombinesUtf16SurrogatePair) {
    const doc_parser::common::DecodedUtf16CodePoint decoded =
        doc_parser::common::decodeUtf16CodePoint(0xD835U, 0xDC37U);

    EXPECT_TRUE(decoded.valid);
    EXPECT_EQ(decoded.code_units, 2);
    EXPECT_EQ(decoded.value, 0x1D437U);
    EXPECT_EQ(doc_parser::common::encodeUtf8(decoded.value), "\xF0\x9D\x90\xB7");
}

TEST(Utf8Test, ReplacesUnpairedSurrogatesAndOutOfRangeValues) {
    const doc_parser::common::DecodedUtf16CodePoint high = doc_parser::common::decodeUtf16CodePoint(0xD835U);
    const doc_parser::common::DecodedUtf16CodePoint low = doc_parser::common::decodeUtf16CodePoint(0xDC37U);

    EXPECT_FALSE(high.valid);
    EXPECT_FALSE(low.valid);
    EXPECT_EQ(high.code_units, 1);
    EXPECT_EQ(low.code_units, 1);
    EXPECT_EQ(doc_parser::common::encodeUtf8(high.value), "\xEF\xBF\xBD");
    EXPECT_EQ(doc_parser::common::encodeUtf8(low.value), "\xEF\xBF\xBD");
    EXPECT_EQ(doc_parser::common::encodeUtf8(0x110000U), "\xEF\xBF\xBD");
}
