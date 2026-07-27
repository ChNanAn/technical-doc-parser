#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/text_model.h"
#include "layout/layout_backend.h"
#include "pipeline/pipeline_context.h"
#include "pipeline/stage_result.h"

#include <vector>

namespace doc_parser::pipeline {

class LayoutAnalysisStage {
public:
    explicit LayoutAnalysisStage(const layout::ILayoutBackend& layout);

    StageResult<std::vector<document::PageLayout>> analyze(const PipelineContext& context,
                                                           const std::vector<document::PageArtifact>& pages,
                                                           const std::vector<document::PageText>& page_texts) const;

private:
    const layout::ILayoutBackend& layout_;
};

} // namespace doc_parser::pipeline
