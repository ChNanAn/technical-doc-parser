#include "pdf/pdf_text_extractor.h"

#include "pdf/pdfium_scoped_handles.h"
#include "pdf/pdfium_runtime.h"

#include <fpdf_text.h>

#include <algorithm>
#include <mutex>
#include <string>

namespace doc_parser::pdf {
namespace {

std::string toUtf8(unsigned int codepoint) {
    std::string result;
    if (codepoint <= 0x7F) {
        result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        result.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        result.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        result.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return result;
}

document::BBox toRenderedBBox(
    double left,
    double right,
    double bottom,
    double top,
    double page_height,
    double scale
) {
    return {
        left * scale,
        (page_height - top) * scale,
        right * scale,
        (page_height - bottom) * scale,
    };
}

void expandBBox(document::BBox& target, const document::BBox& value) {
    target.x0 = std::min(target.x0, value.x0);
    target.y0 = std::min(target.y0, value.y0);
    target.x1 = std::max(target.x1, value.x1);
    target.y1 = std::max(target.y1, value.y1);
}

bool isInlineWhitespace(unsigned int codepoint) {
    return codepoint == ' ' || codepoint == '\t';
}

void startLine(document::PageText& page_text, const document::TextSpan& span) {
    document::TextLine line;
    line.text = span.text;
    line.bbox = span.bbox;
    line.source = span.source;
    line.confidence = span.confidence;
    line.spans.push_back(span);
    page_text.lines.push_back(line);
}

void appendTextToLine(document::PageText& page_text, const std::string& text, const document::BBox& bbox) {
    if (page_text.lines.empty()) {
        return;
    }

    document::TextLine& line = page_text.lines.back();
    line.text += text;
    expandBBox(line.bbox, bbox);
}

void appendSpan(document::PageText& page_text, const document::TextSpan& span) {
    if (page_text.lines.empty()) {
        startLine(page_text, span);
        return;
    }

    document::TextLine& line = page_text.lines.back();
    expandBBox(line.bbox, span.bbox);
    line.spans.push_back(span);
}

class SpanAccumulator {
public:
    void add(document::PageText& page_text, const document::TextSpan& span) {
        appendTextToLine(page_text, span.text, span.bbox);

        if (!has_span_) {
            current_ = span;
            has_span_ = true;
            return;
        }

        current_.text += span.text;
        expandBBox(current_.bbox, span.bbox);
    }

    void flush(document::PageText& page_text) {
        if (!has_span_) {
            return;
        }

        appendSpan(page_text, current_);
        current_ = {};
        has_span_ = false;
    }

private:
    document::TextSpan current_;
    bool has_span_ = false;
};

}  // namespace

bool PdfTextExtractor::extractPageText(
    const PdfReader& reader,
    const TextExtractionRequest& request,
    document::PageText& page_text
) const {
    page_text = {};
    page_text.page_index = request.page_index;
    page_text.page_number = request.page_index + 1;

    if (request.dpi <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(detail::pdfiumMutex());
    if (reader.document_ == nullptr || request.page_index < 0 ||
        request.page_index >= FPDF_GetPageCount(reader.document_)) {
        return false;
    }

    detail::ScopedPdfPage page(FPDF_LoadPage(reader.document_, request.page_index));
    if (page == nullptr) {
        return false;
    }

    detail::ScopedPdfTextPage text_page(FPDFText_LoadPage(page.get()));
    if (text_page == nullptr) {
        return false;
    }

    const double page_height = FPDF_GetPageHeightF(page.get());
    const double scale = static_cast<double>(request.dpi) / 72.0;
    const int char_count = FPDFText_CountChars(text_page.get());
    if (char_count <= 0) {
        return true;
    }

    bool pending_new_line = false;
    SpanAccumulator span_accumulator;
    for (int index = 0; index < char_count; ++index) {
        const unsigned int codepoint = FPDFText_GetUnicode(text_page.get(), index);
        if (codepoint == 0) {
            continue;
        }
        if (codepoint == '\r' || codepoint == '\n') {
            span_accumulator.flush(page_text);
            pending_new_line = true;
            continue;
        }

        double left = 0.0;
        double right = 0.0;
        double bottom = 0.0;
        double top = 0.0;
        if (!FPDFText_GetCharBox(text_page.get(), index, &left, &right, &bottom, &top)) {
            continue;
        }
        if (right < left || top < bottom) {
            continue;
        }

        document::TextSpan span;
        span.text = toUtf8(codepoint);
        span.bbox = toRenderedBBox(left, right, bottom, top, page_height, scale);
        span.source = document::TextSource::PdfTextLayer;
        span.confidence = 1.0;

        if (pending_new_line || page_text.lines.empty()) {
            span_accumulator.flush(page_text);
            startLine(page_text, span);
            page_text.lines.back().text.clear();
            page_text.lines.back().spans.clear();
            pending_new_line = false;
        }

        if (isInlineWhitespace(codepoint)) {
            span_accumulator.flush(page_text);
            appendTextToLine(page_text, span.text, span.bbox);
        } else {
            span_accumulator.add(page_text, span);
        }
    }
    span_accumulator.flush(page_text);

    page_text.has_text = !page_text.lines.empty();
    page_text.preferred_source =
        page_text.has_text ? document::TextSource::PdfTextLayer : document::TextSource::Unknown;

    return true;
}

}  // namespace doc_parser::pdf
