#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"

namespace doc_parser::ocr {

class OcrService {
public:
    bool recognize(const document::PageArtifact& page, int dpi, document::PageText& page_text) const;
};

} // namespace doc_parser::ocr
