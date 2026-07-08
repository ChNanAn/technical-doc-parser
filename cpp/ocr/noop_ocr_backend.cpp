#include "ocr/ocr_backend.h"

namespace doc_parser::ocr {

bool NoopOcrBackend::recognize(const OcrRequest& request, OcrResult& result) const {
    result.page_text = {};
    result.page_text.page_index = request.page.page_index;
    result.page_text.page_number = request.page.page_number;
    result.page_text.has_text = false;
    result.page_text.preferred_source = document::TextSource::Unknown;
    return true;
}

} // namespace doc_parser::ocr
