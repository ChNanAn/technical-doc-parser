#pragma once

#include <array>
#include <string_view>

namespace doc_parser::common::warning_codes {

inline constexpr char kOcrEnhancementFailed[] = "OCR_ENHANCEMENT_FAILED";
inline constexpr char kLayoutBackendFallback[] = "LAYOUT_BACKEND_FALLBACK";
inline constexpr char kTableBackendFallback[] = "TABLE_BACKEND_FALLBACK";

struct Definition {
    std::string_view code;
    std::string_view stage;
    std::string_view summary;
};

inline constexpr std::array<Definition, 3> kRegistry{{
    {
        kOcrEnhancementFailed,
        "text",
        "OCR enhancement failed and usable native text was retained.",
    },
    {
        kLayoutBackendFallback,
        "layout",
        "Layout inference continued with the next configured backend.",
    },
    {
        kTableBackendFallback,
        "table",
        "Table inference continued with the next configured backend.",
    },
}};

constexpr bool isRegistered(std::string_view code) {
    for (const Definition& definition : kRegistry) {
        if (definition.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace doc_parser::common::warning_codes
