#pragma once

#include "document/text_model.h"
#include "pdf/pdf_page_renderer.h"
#include "pdf/pdf_reader.h"

#include <vector>

namespace doc_parser::pipeline {

struct TextExtractionInput {
    const pdf::PdfReader* reader = nullptr;
    const std::vector<pdf::RenderedPage>* pages = nullptr;
    int dpi = 200;
};

class TextExtractionStage {
public:
    bool extract(const TextExtractionInput& input, std::vector<document::PageText>& page_texts) const;
};

}  // namespace doc_parser::pipeline
