#pragma once

#include "common/status.h"

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/reading_order_model.h"
#include "pipeline/pipeline_context.h"
#include "reading_order/reading_order_service.h"

#include <vector>

namespace doc_parser::pipeline {

class ReadingOrderStage {
public:
    explicit ReadingOrderStage(const reading_order::ReadingOrderService& reading_order);

    common::Status order(const PipelineContext& context,
                         const std::vector<document::PageArtifact>& pages,
                         const std::vector<document::PageLayout>& page_layouts,
                         std::vector<document::PageReadingOrder>& page_reading_orders) const;

private:
    const reading_order::ReadingOrderService& reading_order_;
};

} // namespace doc_parser::pipeline
