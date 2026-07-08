#include "pipeline/reading_order_stage.h"

#include <string>

namespace doc_parser::pipeline {

ReadingOrderStage::ReadingOrderStage(const reading_order::IReadingOrderBackend& reading_order)
    : reading_order_(reading_order) {}

common::Status ReadingOrderStage::order(const PipelineContext& context,
                                        const std::vector<document::PageArtifact>& pages,
                                        const std::vector<document::PageLayout>& page_layouts,
                                        std::vector<document::PageReadingOrder>& page_reading_orders) const {
    (void)context;
    page_reading_orders.clear();

    if (pages.size() != page_layouts.size()) {
        return common::Status::error("reading_order.page_count_mismatch",
                                     "page artifact count does not match layout count");
    }

    page_reading_orders.reserve(pages.size());
    for (std::size_t index = 0; index < pages.size(); ++index) {
        reading_order::ReadingOrderResult result;
        if (!reading_order_.order({pages[index], page_layouts[index]}, result)) {
            return common::Status::error("reading_order.backend_failed",
                                         "reading order failed for page " + std::to_string(index + 1));
        }
        page_reading_orders.push_back(result.reading_order);
    }

    return common::Status::ok();
}

} // namespace doc_parser::pipeline
