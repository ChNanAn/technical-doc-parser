#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"
#include "ocr/ocr_backend.h"

#include <memory>

namespace doc_parser::ocr {

class OcrService {
public:
    explicit OcrService(const IOcrBackend& backend);
    explicit OcrService(std::unique_ptr<IOcrBackend> backend);

    bool recognize(const document::PageArtifact& page, int dpi, document::PageText& page_text) const;

private:
    std::unique_ptr<IOcrBackend> owned_backend_;
    const IOcrBackend* backend_ = nullptr;
};

} // namespace doc_parser::ocr
