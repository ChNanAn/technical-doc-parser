#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"

namespace doc_parser::ocr {

struct OcrRequest {
    const document::PageArtifact& page;
    int dpi = 200;
};

struct OcrResult {
    document::PageText page_text;
};

class IOcrBackend {
public:
    virtual ~IOcrBackend() = default;

    virtual bool recognize(const OcrRequest& request, OcrResult& result) const = 0;
};

class NoopOcrBackend final : public IOcrBackend {
public:
    bool recognize(const OcrRequest& request, OcrResult& result) const override;
};

} // namespace doc_parser::ocr
