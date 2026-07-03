#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"
#include "ocr/ocr_service.h"
#include "pipeline/pipeline_context.h"
#include "pipeline/stage_interfaces.h"

#include <vector>

namespace doc_parser::pipeline {

// 文本提取策略阶段：优先使用文档后端的原生文本，空页再交给 OCR fallback。
class TextExtractionStage {
public:
    TextExtractionStage(const IDocumentBackend& document_backend, const ocr::OcrService& ocr);

    bool extract(const PipelineContext& context,
                 const std::vector<document::PageArtifact>& pages,
                 std::vector<document::PageText>& page_texts) const;

private:
    const IDocumentBackend& document_backend_;
    const ocr::OcrService& ocr_;
};

} // namespace doc_parser::pipeline
