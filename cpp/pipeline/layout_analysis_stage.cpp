#include "pipeline/layout_analysis_stage.h"

#include "layout/layout_postprocessing.h"

#include <iterator>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

namespace doc_parser::pipeline {

LayoutAnalysisStage::LayoutAnalysisStage(const layout::ILayoutBackend& layout) : layout_(layout) {}

StageResult<std::vector<document::PageLayout>>
LayoutAnalysisStage::analyze(const PipelineContext& context,
                             const std::vector<document::PageArtifact>& pages,
                             const std::vector<document::PageText>& page_texts) const {
    (void)context;
    StageResult<std::vector<document::PageLayout>> analysis;

    if (pages.size() != page_texts.size()) {
        analysis.status =
            common::Status::error("layout.page_count_mismatch", "text page count does not match page artifacts");
        return analysis;
    }

    analysis.value.reserve(pages.size());
    for (std::size_t index = 0; index < pages.size(); ++index) {
        layout::LayoutResult result;
        if (!layout_.analyze({pages[index], page_texts[index]}, result)) {
            analysis.status = common::Status::error("layout.analysis_failed",
                                                    "layout analysis failed for page " + std::to_string(index + 1));
            return analysis;
        }
        const layout::detail::LayoutRefinementStats refinement =
            layout::detail::refineMultiColumnTextLineOrder(page_texts[index], result.layout.blocks);
        if (refinement.reordered_blocks > 0) {
            spdlog::debug("layout_refinement: page={} reordered_blocks={} max_columns={}",
                          pages[index].page_number,
                          refinement.reordered_blocks,
                          refinement.maximum_columns);
        }
        const layout::detail::EdgeFurnitureRefinementStats furniture =
            layout::detail::refineEdgeFurniture(pages[index], result.layout.blocks);
        if (furniture.headers > 0 || furniture.footers > 0) {
            spdlog::debug("layout_furniture_refinement: page={} headers={} footers={}",
                          pages[index].page_number,
                          furniture.headers,
                          furniture.footers);
        }
        analysis.value.push_back(std::move(result.layout));
        analysis.diagnostics.insert(analysis.diagnostics.end(),
                                    std::make_move_iterator(result.diagnostics.begin()),
                                    std::make_move_iterator(result.diagnostics.end()));
    }

    return analysis;
}

} // namespace doc_parser::pipeline
