#include "pipeline/reading_order_stage.h"

#include <string>
#include <utility>

namespace doc_parser::pipeline {

ReadingOrderStage::ReadingOrderStage(const reading_order::IReadingOrderBackend& reading_order)
    : reading_order_(reading_order) {}

StageResult<std::vector<document::PageReadingOrder>>
ReadingOrderStage::order(const PipelineContext& context,
                         const std::vector<document::PageArtifact>& pages,
                         const std::vector<document::PageLayout>& page_layouts) const {
    (void)context;
    StageResult<std::vector<document::PageReadingOrder>> ordering;

    if (pages.size() != page_layouts.size()) {
        ordering.status = common::Status::error("reading_order.page_count_mismatch",
                                                "page artifact count does not match layout count");
        return ordering;
    }

    ordering.value.reserve(pages.size());
    for (std::size_t index = 0; index < pages.size(); ++index) {
        reading_order::ReadingOrderResult result;
        if (!reading_order_.order({pages[index], page_layouts[index]}, result)) {
            ordering.status = common::Status::error("reading_order.backend_failed",
                                                    "reading order failed for page " + std::to_string(index + 1));
            return ordering;
        }
        ordering.value.push_back(std::move(result.reading_order));
    }

    return ordering;
}

} // namespace doc_parser::pipeline
