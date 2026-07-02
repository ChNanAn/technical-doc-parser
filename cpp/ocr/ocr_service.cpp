#include "ocr/ocr_service.h"

namespace doc_parser::ocr {

bool OcrService::recognize(const document::PageArtifact& page, int dpi, document::PageText& page_text) const {
    page_text = {};
    page_text.page_index = page.page_index;
    page_text.page_number = page.page_number;

    if (dpi <= 0 || page.output_path.empty()) {
        return false;
    }

    // TODO(ocr): plug in the OCR adapter here. The fallback is intentionally
    // non-failing so scanned PDFs still produce page/image artifacts today.
    page_text.has_text = false;
    page_text.preferred_source = document::TextSource::Unknown;
    return true;
}

} // namespace doc_parser::ocr
