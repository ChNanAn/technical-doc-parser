#include "ocr/ocr_service.h"

#include "ocr/tesseract_cli_ocr_backend.h"

#include <utility>

namespace doc_parser::ocr {
namespace {

const IOcrBackend& defaultOcrBackend() {
    static const TesseractCliOcrBackend tesseract_backend;
    if (tesseract_backend.isAvailable()) {
        return tesseract_backend;
    }

    static const NoopOcrBackend backend;
    return backend;
}

} // namespace

bool NoopOcrBackend::recognize(const OcrRequest& request, OcrResult& result) const {
    result.page_text = {};
    result.page_text.page_index = request.page.page_index;
    result.page_text.page_number = request.page.page_number;
    result.page_text.has_text = false;
    result.page_text.preferred_source = document::TextSource::Unknown;
    return true;
}

OcrService::OcrService() : OcrService(defaultOcrBackend()) {}

OcrService::OcrService(const IOcrBackend& backend) : backend_(&backend) {}

OcrService::OcrService(std::unique_ptr<IOcrBackend> backend)
    : owned_backend_(std::move(backend)), backend_(owned_backend_.get()) {}

bool OcrService::recognize(const document::PageArtifact& page, int dpi, document::PageText& page_text) const {
    page_text = {};
    page_text.page_index = page.page_index;
    page_text.page_number = page.page_number;

    if (backend_ == nullptr || dpi <= 0 || page.output_path.empty()) {
        return false;
    }

    OcrResult result;
    if (!backend_->recognize({page, dpi}, result)) {
        return false;
    }

    page_text = result.page_text;
    return true;
}

} // namespace doc_parser::ocr
