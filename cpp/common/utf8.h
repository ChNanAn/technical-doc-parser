#pragma once

#include <cstdint>
#include <string>

namespace doc_parser::common {

constexpr std::uint32_t kUnicodeReplacementCharacter = 0xFFFDU;

struct DecodedUtf16CodePoint {
    std::uint32_t value = kUnicodeReplacementCharacter;
    int code_units = 1;
    bool valid = false;
};

inline bool isHighSurrogate(std::uint32_t value) { return value >= 0xD800U && value <= 0xDBFFU; }

inline bool isLowSurrogate(std::uint32_t value) { return value >= 0xDC00U && value <= 0xDFFFU; }

inline bool isUnicodeScalarValue(std::uint32_t value) {
    return value <= 0x10FFFFU && !isHighSurrogate(value) && !isLowSurrogate(value);
}

inline DecodedUtf16CodePoint decodeUtf16CodePoint(std::uint32_t first, std::uint32_t following = 0) {
    if (isHighSurrogate(first)) {
        if (!isLowSurrogate(following)) {
            return {};
        }
        return {
            0x10000U + ((first - 0xD800U) << 10U) + (following - 0xDC00U),
            2,
            true,
        };
    }
    if (!isUnicodeScalarValue(first)) {
        return {};
    }
    return {first, 1, true};
}

inline std::string encodeUtf8(std::uint32_t codepoint) {
    if (!isUnicodeScalarValue(codepoint)) {
        codepoint = kUnicodeReplacementCharacter;
    }

    std::string result;
    if (codepoint <= 0x7FU) {
        result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFU) {
        result.push_back(static_cast<char>(0xC0U | ((codepoint >> 6U) & 0x1FU)));
        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint <= 0xFFFFU) {
        result.push_back(static_cast<char>(0xE0U | ((codepoint >> 12U) & 0x0FU)));
        result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else {
        result.push_back(static_cast<char>(0xF0U | ((codepoint >> 18U) & 0x07U)));
        result.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
    return result;
}

} // namespace doc_parser::common
