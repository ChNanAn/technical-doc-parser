#pragma once

#include "document/text_model.h"
#include "pdf/pdf_reader.h"
#include "pdf/text_service.h"

#include <vector>

namespace doc_parser::pipeline {

// 拥有 PDF / OCR 调度策略。OCR 暂未接入，目前只走 TextService。
class TextExtractionStage {
public:
    TextExtractionStage() = default;

    bool extract(const pdf::PdfReader& source,
                 int dpi,
                 std::vector<document::PageText>& page_texts) const;

private:
    pdf::TextService text_;
};

}  // namespace doc_parser::pipeline
