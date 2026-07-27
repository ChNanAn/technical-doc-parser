#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"
#include "document_source/document_source_interfaces.h"
#include "ocr/ocr_backend.h"
#include "pipeline/pipeline_context.h"
#include "pipeline/stage_result.h"

#include <vector>

namespace doc_parser::pipeline {

// 文本提取策略阶段：评估原生文本质量，并按页选择原生文本、OCR 或坐标去重合并。
class TextExtractionStage {
public:
    TextExtractionStage(const document_source::INativeTextExtractor* native_text_extractor,
                        const ocr::IOcrBackend& ocr);

    StageResult<std::vector<document::PageText>> extract(const PipelineContext& context,
                                                         const std::vector<document::PageArtifact>& pages) const;

private:
    const document_source::INativeTextExtractor* native_text_extractor_ = nullptr;
    const ocr::IOcrBackend& ocr_;
};

} // namespace doc_parser::pipeline
