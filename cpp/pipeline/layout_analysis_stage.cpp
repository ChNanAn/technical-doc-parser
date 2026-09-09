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
        auto page = analyzePage(context, pages[index], page_texts[index]);
        if (!page.ok()) {
            analysis.status = page.status;
            return analysis;
        }
        analysis.value.push_back(std::move(page.value));
        analysis.diagnostics.insert(analysis.diagnostics.end(), page.diagnostics.begin(), page.diagnostics.end());
    }
    return analysis;
}

StageResult<document::PageLayout> LayoutAnalysisStage::analyzePage(const PipelineContext& context,
                                                                   const document::PageArtifact& page,
                                                                   const document::PageText& text) const {
    (void)context;
    StageResult<document::PageLayout> analysis;
    layout::LayoutResult result;
    if (!layout_.analyze({page, text}, result)) {
        analysis.status = common::Status::error("layout.analysis_failed",
                                                "layout analysis failed for page " + std::to_string(page.page_number));
        return analysis;
    }
    const layout::detail::LayoutRefinementStats refinement =
        layout::detail::refineMultiColumnTextLineOrder(text, result.layout.blocks);
    if (refinement.reordered_blocks > 0) {
        spdlog::debug("layout_refinement: page={} reordered_blocks={} max_columns={}",
                      page.page_number,
                      refinement.reordered_blocks,
                      refinement.maximum_columns);
    }
    const layout::detail::EdgeFurnitureRefinementStats furniture =
        layout::detail::refineEdgeFurniture(page, result.layout.blocks);
    if (furniture.headers > 0 || furniture.footers > 0) {
        spdlog::debug("layout_furniture_refinement: page={} headers={} footers={}",
                      page.page_number,
                      furniture.headers,
                      furniture.footers);
    }
    analysis.value = std::move(result.layout);
    analysis.diagnostics = std::move(result.diagnostics);

    return analysis;
}

} // namespace doc_parser::pipeline
