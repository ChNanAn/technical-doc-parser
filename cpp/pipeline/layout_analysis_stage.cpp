#include "pipeline/layout_analysis_stage.h"

namespace doc_parser::pipeline {

LayoutAnalysisStage::LayoutAnalysisStage(const layout::LayoutService& layout) : layout_(layout) {}

bool LayoutAnalysisStage::analyze(const PipelineContext& context,
                                  const std::vector<document::PageArtifact>& pages,
                                  const std::vector<document::PageText>& page_texts,
                                  std::vector<document::PageLayout>& page_layouts) const {
    (void)context;
    page_layouts.clear();

    if (pages.size() != page_texts.size()) {
        return false;
    }

    page_layouts.reserve(pages.size());
    for (std::size_t index = 0; index < pages.size(); ++index) {
        document::PageLayout layout;
        if (!layout_.analyze(pages[index], page_texts[index], layout)) {
            return false;
        }
        page_layouts.push_back(layout);
    }

    return true;
}

} // namespace doc_parser::pipeline
