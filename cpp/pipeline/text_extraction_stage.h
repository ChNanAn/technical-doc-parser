#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"
#include "ocr/ocr_service.h"
#include "pdf/pdf_document.h"
#include "pdf/text_service.h"

#include <vector>

namespace doc_parser::pipeline {

// 拥有 PDF / OCR 调度策略。优先 PDF text layer，空页再交给 OCR fallback。
class TextExtractionStage {
public:
    TextExtractionStage() = default;

    bool extract(const pdf::PdfDocument& source,
                 const std::vector<document::PageArtifact>& pages,
                 int dpi,
                 std::vector<document::PageText>& page_texts) const;

private:
    pdf::TextService text_;
    ocr::OcrService ocr_;
};

} // namespace doc_parser::pipeline
