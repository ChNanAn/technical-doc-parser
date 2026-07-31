#include "pipeline/reading_order_stage.h"

#include <algorithm>
#include <spdlog/spdlog.h>
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
        int maximum_columns = 0;
        int maximum_band = -1;
        for (const document::ReadingOrderPlacement& placement : result.reading_order.trace.placements) {
            maximum_columns = std::max(maximum_columns, placement.column_end + 1);
            maximum_band = std::max(maximum_band, placement.band_index);
        }
        spdlog::debug("reading_order: page={} algorithm={} bands={} max_columns={} placements={} cycle_breaks={}",
                      pages[index].page_number,
                      result.reading_order.trace.algorithm,
                      maximum_band + 1,
                      maximum_columns,
                      result.reading_order.trace.placements.size(),
                      result.reading_order.trace.cycle_breaks.size());
        for (const document::ReadingOrderCycleBreak& cycle_break : result.reading_order.trace.cycle_breaks) {
            spdlog::debug("reading_order: page={} cycle_break={} -> {} reason={} confidence={:.2f}",
                          pages[index].page_number,
                          cycle_break.from_layout_block_id,
                          cycle_break.to_layout_block_id,
                          cycle_break.reason,
                          cycle_break.confidence);
        }
        ordering.value.push_back(std::move(result.reading_order));
    }

    return ordering;
}

} // namespace doc_parser::pipeline
