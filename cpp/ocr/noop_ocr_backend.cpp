#include "ocr/ocr_backend.h"

#include <utility>

namespace doc_parser::ocr {

bool NoopOcrBackend::recognize(const OcrRequest& request, OcrResult& result) const {
    result = {};
    result.page_text = {};
    result.page_text.page_index = request.page.page_index;
    result.page_text.page_number = request.page.page_number;
    result.page_text.has_text = false;
    result.page_text.preferred_source = document::TextSource::Unknown;
    return true;
}

UnavailableOcrBackend::UnavailableOcrBackend(std::string reason) : reason_(std::move(reason)) {}

bool UnavailableOcrBackend::recognize(const OcrRequest& request, OcrResult& result) const {
    result = {};
    result.page_text.page_index = request.page.page_index;
    result.page_text.page_number = request.page.page_number;
    return false;
}

std::string UnavailableOcrBackend::unavailableReason() const { return reason_; }

} // namespace doc_parser::ocr
