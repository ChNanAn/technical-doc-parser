#pragma once

#include "common/status.h"

#include "backend/document_backend_interfaces.h"
#include "document/page_artifact.h"
#include "document/text_model.h"
#include "ocr/ocr_service.h"
#include "pipeline/pipeline_context.h"

#include <vector>

namespace doc_parser::pipeline {

// 文本提取策略阶段：优先使用文档后端的原生文本，空页再交给 OCR fallback。
class TextExtractionStage {
public:
    TextExtractionStage(const INativeTextExtractor* native_text_extractor, const ocr::OcrService& ocr);

    common::Status extract(const PipelineContext& context,
                           const std::vector<document::PageArtifact>& pages,
                           std::vector<document::PageText>& page_texts) const;

private:
    const INativeTextExtractor* native_text_extractor_ = nullptr;
    const ocr::OcrService& ocr_;
};

} // namespace doc_parser::pipeline
