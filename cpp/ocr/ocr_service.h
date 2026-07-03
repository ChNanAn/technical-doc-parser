#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"
#include "ocr/ocr_backend.h"

namespace doc_parser::ocr {

class OcrService {
public:
    OcrService();
    explicit OcrService(const IOcrBackend& backend);

    bool recognize(const document::PageArtifact& page, int dpi, document::PageText& page_text) const;

private:
    const IOcrBackend* backend_ = nullptr;
};

} // namespace doc_parser::ocr
