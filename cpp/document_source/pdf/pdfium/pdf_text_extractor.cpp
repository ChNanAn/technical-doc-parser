#include "document_source/pdf/pdfium/pdf_text_extractor.h"

#include "common/utf8.h"

#include "document/text_normalizer.h"
#include "document_source/pdf/pdfium/pdfium_runtime.h"
#include "document_source/pdf/pdfium/pdfium_scoped_handles.h"

#include <algorithm>
#include <fpdf_text.h>
#include <mutex>
#include <string>
#include <vector>

namespace doc_parser::pdf {
namespace {

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
    for (int index = 0; index < char_count;) {
        const unsigned int first = FPDFText_GetUnicode(text_page.get(), index);
        const unsigned int following = index + 1 < char_count ? FPDFText_GetUnicode(text_page.get(), index + 1) : 0;
        const common::DecodedUtf16CodePoint decoded = common::decodeUtf16CodePoint(first, following);
        const int first_index = index;
        index += decoded.code_units;
        std::uint32_t codepoint = decoded.value;
        if (codepoint == 0) {
            continue;
        }
        ++page_text.extraction_signals.decoded_codepoints;
        if (!decoded.valid) {
            ++page_text.extraction_signals.invalid_utf16_codepoints;
        }
        if (codepoint == common::kUnicodeReplacementCharacter) {
            ++page_text.extraction_signals.replacement_codepoints;
        }
        if (codepoint == '\r' || codepoint == '\n') {
            ++page_text.extraction_signals.c0_control_counts[codepoint];
            document::TextToken line_break;
            line_break.kind = document::TextTokenKind::LineBreak;
            tokens.push_back(line_break);
            continue;
        }
        if (codepoint < 0x20U) {
            ++page_text.extraction_signals.c0_control_counts[codepoint];
        }
        if (codepoint == 0x02U) {
            codepoint = ' ';
        } else if (codepoint < 0x20U && codepoint != '\t') {
            continue;
        }

        double left = 0.0;
        double right = 0.0;
        double bottom = 0.0;
        double top = 0.0;
        if (!FPDFText_GetCharBox(text_page.get(), first_index, &left, &right, &bottom, &top)) {
            continue;
        }
        if (decoded.code_units == 2) {
            double pair_left = 0.0;
            double pair_right = 0.0;
            double pair_bottom = 0.0;
            double pair_top = 0.0;
            if (FPDFText_GetCharBox(
                    text_page.get(), first_index + 1, &pair_left, &pair_right, &pair_bottom, &pair_top)) {
                left = std::min(left, pair_left);
                right = std::max(right, pair_right);
                bottom = std::min(bottom, pair_bottom);
                top = std::max(top, pair_top);
            }
        }
        if (right < left || top < bottom) {
            continue;
        }

        tokens.push_back({
            document::TextTokenKind::Glyph,
            common::encodeUtf8(codepoint),
            toRenderedBBox(left, right, bottom, top, page_height, scale),
            document::TextSource::PdfTextLayer,
            1.0,
        });
    }

    const document::NativeTextExtractionSignals extraction_signals = page_text.extraction_signals;
    page_text = document::TextNormalizer().normalize(request.page_index, tokens);
    page_text.extraction_signals = extraction_signals;
    return true;
}

} // namespace doc_parser::pdf
