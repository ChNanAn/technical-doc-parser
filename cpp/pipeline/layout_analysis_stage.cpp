#include "pipeline/layout_analysis_stage.h"

#include <string>

namespace doc_parser::pipeline {

LayoutAnalysisStage::LayoutAnalysisStage(const layout::ILayoutBackend& layout) : layout_(layout) {}

common::Status LayoutAnalysisStage::analyze(const PipelineContext& context,
                                            const std::vector<document::PageArtifact>& pages,
                                            const std::vector<document::PageText>& page_texts,
                                            std::vector<document::PageLayout>& page_layouts) const {
    (void)context;
    page_layouts.clear();

    if (pages.size() != page_texts.size()) {
        return common::Status::error("layout.page_count_mismatch", "text page count does not match page artifacts");
    }

    page_layouts.reserve(pages.size());
    for (std::size_t index = 0; index < pages.size(); ++index) {
        layout::LayoutResult result;
        if (!layout_.analyze({pages[index], page_texts[index]}, result)) {
            return common::Status::error("layout.analysis_failed",
                                         "layout analysis failed for page " + std::to_string(index + 1));
        }
        page_layouts.push_back(result.layout);
    }

    return common::Status::ok();
}

} // namespace doc_parser::pipeline
