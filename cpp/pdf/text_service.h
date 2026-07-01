#pragma once

#include "document/text_model.h"
#include "pdf/pdf_reader.h"

#include <vector>

namespace doc_parser::pdf {

// 文本提取操作。无状态，操作通过 const PdfReader& 接收 PDF 源。
class TextService {
public:
    TextService() = default;

    bool extractText(const PdfReader& source,
                     int dpi,
                     std::vector<document::PageText>& page_texts) const;
};

}  // namespace doc_parser::pdf
