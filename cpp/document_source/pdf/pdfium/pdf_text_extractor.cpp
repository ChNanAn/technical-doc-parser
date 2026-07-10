#include "document_source/pdf/pdfium/pdf_text_extractor.h"

#include "document/text_normalizer.h"
#include "document_source/pdf/pdfium/pdfium_runtime.h"
#include "document_source/pdf/pdfium/pdfium_scoped_handles.h"

#include <fpdf_text.h>
#include <mutex>
#include <string>
#include <vector>

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

document::BBox toRenderedBBox(double left, double right, double bottom, double top, double page_height, double scale) {
    return {
        left * scale,
        (page_height - top) * scale,
        right * scale,
        (page_height - bottom) * scale,
    };
}

} // namespace

bool PdfTextExtractor::extractPageText(const PdfReader& reader,
                                       const TextExtractionRequest& request,
                                       document::PageText& page_text) const {
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
        page_text = document::TextNormalizer().normalize(request.page_index, {});
        return true;
    }

    std::vector<document::TextToken> tokens;
    tokens.reserve(static_cast<std::size_t>(char_count));
    for (int index = 0; index < char_count; ++index) {
        const unsigned int codepoint = FPDFText_GetUnicode(text_page.get(), index);
        if (codepoint == 0) {
            continue;
        }
        if (codepoint == '\r' || codepoint == '\n') {
            document::TextToken line_break;
            line_break.kind = document::TextTokenKind::LineBreak;
            tokens.push_back(line_break);
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

        tokens.push_back({
            document::TextTokenKind::Glyph,
            toUtf8(codepoint),
            toRenderedBBox(left, right, bottom, top, page_height, scale),
            document::TextSource::PdfTextLayer,
            1.0,
        });
    }

    page_text = document::TextNormalizer().normalize(request.page_index, tokens);
    return true;
}

} // namespace doc_parser::pdf
