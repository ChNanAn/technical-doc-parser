#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace doc_parser::document {

struct BBox {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
};

enum class TextSource {
    Unknown,
    PdfTextLayer,
    Ocr,
    Mixed,
};

struct TextSpan {
    std::string text;
    BBox bbox;
    TextSource source = TextSource::Unknown;
    double confidence = 1.0;
};

struct TextLine {
    std::string text;
    BBox bbox;
    TextSource source = TextSource::Unknown;
    double confidence = 1.0;
    std::vector<TextSpan> spans;
};

struct NativeTextExtractionSignals {
    std::size_t decoded_codepoints = 0;
    std::size_t invalid_utf16_codepoints = 0;
    std::size_t replacement_codepoints = 0;
    std::array<std::size_t, 32> c0_control_counts{};
};

struct PageText {
    int page_index = 0;
    int page_number = 0;
    bool has_text = false;
    TextSource preferred_source = TextSource::Unknown;
    std::vector<TextLine> lines;
    NativeTextExtractionSignals extraction_signals;
};

struct SourceReference {
    std::string page_id;
    BBox bbox;
    std::string text;
    TextSource source = TextSource::Unknown;
};

} // namespace doc_parser::document
