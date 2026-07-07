#pragma once

#include "backend/pdf/pdf_document.h"
#include "document/text_model.h"

#include <vector>

namespace doc_parser::pdf {

// 文本提取操作。无状态，操作通过 const PdfDocument& 接收 PDF 源。
class TextService {
public:
    TextService() = default;

    bool extractText(const PdfDocument& source, int dpi, std::vector<document::PageText>& page_texts) const;
};

} // namespace doc_parser::pdf
