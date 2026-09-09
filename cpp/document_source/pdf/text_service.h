#pragma once

#include "document/text_model.h"
#include "document_source/pdf/pdf_document.h"

#include <vector>

namespace doc_parser::pdf {

// 文本提取操作。无状态，操作通过 const PdfDocument& 接收 PDF 源。
class TextService {
public:
    TextService() = default;

    bool extractText(const PdfDocument& source, int dpi, std::vector<document::PageText>& page_texts) const;
    bool extractPageText(const PdfDocument& source, int dpi, int page_index, document::PageText& page_text) const;
};

} // namespace doc_parser::pdf
