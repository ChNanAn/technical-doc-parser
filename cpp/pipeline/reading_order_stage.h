#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/reading_order_model.h"
#include "pipeline/pipeline_context.h"
#include "pipeline/stage_result.h"
#include "reading_order/reading_order_backend.h"

#include <vector>

namespace doc_parser::pipeline {

class ReadingOrderStage {
public:
    explicit ReadingOrderStage(const reading_order::IReadingOrderBackend& reading_order);

    StageResult<std::vector<document::PageReadingOrder>>
    order(const PipelineContext& context,
          const std::vector<document::PageArtifact>& pages,
          const std::vector<document::PageLayout>& page_layouts) const;

private:
    const reading_order::IReadingOrderBackend& reading_order_;
};

} // namespace doc_parser::pipeline
