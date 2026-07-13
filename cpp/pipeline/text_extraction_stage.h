#pragma once

#include "common/status.h"

#include "document/page_artifact.h"
#include "document/text_model.h"
#include "document_source/document_source_interfaces.h"
#include "ocr/ocr_backend.h"
#include "pipeline/pipeline_context.h"

#include <vector>

namespace doc_parser::pipeline {

// 文本提取策略阶段：优先使用文档后端的原生文本，空页再交给 OCR fallback。
class TextExtractionStage {
public:
    TextExtractionStage(const document_source::INativeTextExtractor* native_text_extractor,
                        const ocr::IOcrBackend& ocr);

    common::Status extract(const PipelineContext& context,
                           const std::vector<document::PageArtifact>& pages,
                           std::vector<document::PageText>& page_texts) const;

private:
    const document_source::INativeTextExtractor* native_text_extractor_ = nullptr;
    const ocr::IOcrBackend& ocr_;
};

} // namespace doc_parser::pipeline
