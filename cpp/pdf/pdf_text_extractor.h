#pragma once

#include "document/text_model.h"
#include "pdf/pdf_reader.h"

namespace doc_parser::pdf {

struct TextExtractionRequest {
    int page_index = 0;
    int dpi = 200;
};

class PdfTextExtractor {
public:
    bool extractPageText(
        const PdfReader& reader,
        const TextExtractionRequest& request,
        document::PageText& page_text
    ) const;
};

}  // namespace doc_parser::pdf
