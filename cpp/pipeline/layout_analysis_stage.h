#pragma once

#include "common/result.h"

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/text_model.h"
#include "layout/layout_service.h"
#include "pipeline/pipeline_context.h"

#include <vector>

namespace doc_parser::pipeline {

class LayoutAnalysisStage {
public:
    explicit LayoutAnalysisStage(const layout::LayoutService& layout);

    common::Status analyze(const PipelineContext& context,
                           const std::vector<document::PageArtifact>& pages,
                           const std::vector<document::PageText>& page_texts,
                           std::vector<document::PageLayout>& page_layouts) const;

private:
    const layout::LayoutService& layout_;
};

} // namespace doc_parser::pipeline
