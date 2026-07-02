#pragma once

#include "document/text_model.h"

#include <string>
#include <vector>

namespace doc_parser::document {

enum class TextTokenKind {
    Glyph,
    LineBreak,
};

struct TextToken {
    TextTokenKind kind = TextTokenKind::Glyph;
    std::string text;
    BBox bbox;
    TextSource source = TextSource::Unknown;
    double confidence = 1.0;
};

class TextNormalizer {
public:
    PageText normalize(int page_index, const std::vector<TextToken>& tokens) const;
};

} // namespace doc_parser::document
