#include "document/text_normalizer.h"

#include <algorithm>
#include <cmath>

namespace doc_parser::document {
namespace {

bool isInlineWhitespace(const TextToken& token) {
    return !token.text.empty() &&
           std::all_of(token.text.begin(), token.text.end(), [](char value) { return value == ' ' || value == '\t'; });
}

TextSpan toSpan(const TextToken& token) {
    return {
        token.text,
        token.bbox,
        token.source,
        token.confidence,
    };
}

void expandBBox(BBox& target, const BBox& value) {
    target.x0 = std::min(target.x0, value.x0);
    target.y0 = std::min(target.y0, value.y0);
    target.x1 = std::max(target.x1, value.x1);
    target.y1 = std::max(target.y1, value.y1);
}

void startLine(PageText& page_text, const TextSpan& span) {
    TextLine line;
    line.text = span.text;
    line.bbox = span.bbox;
    line.source = span.source;
    line.confidence = span.confidence;
    line.spans.push_back(span);
    page_text.lines.push_back(line);
}

void appendTextToLine(PageText& page_text, const std::string& text, const BBox& bbox) {
    if (page_text.lines.empty()) {
        return;
    }

    TextLine& line = page_text.lines.back();
    line.text += text;
    expandBBox(line.bbox, bbox);
}

void appendWhitespaceToLine(PageText& page_text, const std::string& text) {
    if (!page_text.lines.empty()) {
        page_text.lines.back().text += text;
    }
}

double bboxHeight(const BBox& bbox) { return std::max(0.0, bbox.y1 - bbox.y0); }

double verticalOverlap(const BBox& lhs, const BBox& rhs) {
    return std::max(0.0, std::min(lhs.y1, rhs.y1) - std::max(lhs.y0, rhs.y0));
}

bool startsNewGeometricLine(const BBox& previous, const BBox& current) {
    const double previous_height = bboxHeight(previous);
    const double current_height = bboxHeight(current);
    const double reference_height = std::max(previous_height, current_height);
    const double shorter_height = std::min(previous_height, current_height);
    if (reference_height <= 0.0 || shorter_height <= 0.0) {
        return false;
    }

    const double overlap_ratio = verticalOverlap(previous, current) / shorter_height;
    const double center_delta = std::abs((previous.y0 + previous.y1) * 0.5 - (current.y0 + current.y1) * 0.5);
    if (overlap_ratio < 0.20 && center_delta > reference_height * 0.80) {
        return true;
    }

    const double forward_gap = current.x0 - previous.x1;
    const double backward_gap = previous.x0 - current.x1;
    return overlap_ratio >= 0.50 && (forward_gap > reference_height * 6.0 || backward_gap > reference_height * 2.0);
}

std::vector<TextLine>::const_iterator findFirstTextLine(const std::vector<TextLine>& lines) {
    return std::find_if(lines.begin(), lines.end(), [](const TextLine& line) { return !line.spans.empty(); });
}

void appendSpan(PageText& page_text, const TextSpan& span) {
    if (page_text.lines.empty()) {
        startLine(page_text, span);
        return;
    }

    TextLine& line = page_text.lines.back();
    expandBBox(line.bbox, span.bbox);
    line.spans.push_back(span);
}

class SpanAccumulator {
public:
    void add(PageText& page_text, const TextSpan& span) {
        appendTextToLine(page_text, span.text, span.bbox);

        if (!has_span_) {
            current_ = span;
            has_span_ = true;
            return;
        }

        current_.text += span.text;
        expandBBox(current_.bbox, span.bbox);
    }

    void flush(PageText& page_text) {
        if (!has_span_) {
            return;
        }

        appendSpan(page_text, current_);
        current_ = {};
        has_span_ = false;
    }

private:
    TextSpan current_;
    bool has_span_ = false;
};

} // namespace

PageText TextNormalizer::normalize(int page_index, const std::vector<TextToken>& tokens) const {
    PageText page_text;
    page_text.page_index = page_index;
    page_text.page_number = page_index + 1;

    bool pending_new_line = false;
    bool has_previous_glyph = false;
    BBox previous_glyph_bbox;
    SpanAccumulator span_accumulator;
    for (const auto& token : tokens) {
        if (token.kind == TextTokenKind::LineBreak) {
            span_accumulator.flush(page_text);
            pending_new_line = true;
            has_previous_glyph = false;
            continue;
        }
        if (token.text.empty()) {
            continue;
        }

        const bool whitespace = isInlineWhitespace(token);
        if (whitespace && (page_text.lines.empty() || pending_new_line)) {
            continue;
        }

        const TextSpan span = toSpan(token);
        if (!whitespace && has_previous_glyph && startsNewGeometricLine(previous_glyph_bbox, span.bbox)) {
            span_accumulator.flush(page_text);
            pending_new_line = true;
        }
        if (pending_new_line || page_text.lines.empty()) {
            span_accumulator.flush(page_text);
            startLine(page_text, span);
            page_text.lines.back().text.clear();
            page_text.lines.back().spans.clear();
            pending_new_line = false;
        }

        if (whitespace) {
            span_accumulator.flush(page_text);
            appendWhitespaceToLine(page_text, span.text);
        } else {
            span_accumulator.add(page_text, span);
            previous_glyph_bbox = span.bbox;
            has_previous_glyph = true;
        }
    }
    span_accumulator.flush(page_text);

    const auto first_text_line = findFirstTextLine(page_text.lines);
    page_text.has_text = first_text_line != page_text.lines.end();
    page_text.preferred_source = page_text.has_text ? first_text_line->source : TextSource::Unknown;
    return page_text;
}

} // namespace doc_parser::document
