#include "pipeline/table_recognition_stage.h"

#include <string>

namespace doc_parser::pipeline {

TableRecognitionStage::TableRecognitionStage(const table::TableService& table) : table_(table) {}

common::Status TableRecognitionStage::recognize(const PipelineContext& context,
                                                const std::vector<document::PageArtifact>& pages,
                                                const std::vector<document::PageText>& page_texts,
                                                const std::vector<document::PageLayout>& page_layouts,
                                                std::vector<document::PageTables>& page_tables) const {
    (void)context;
    page_tables.clear();

    if (pages.size() != page_texts.size() || pages.size() != page_layouts.size()) {
        return common::Status::error("table.page_count_mismatch",
                                     "page, text, and layout counts must match before table recognition");
    }

    page_tables.reserve(pages.size());
    for (std::size_t index = 0; index < pages.size(); ++index) {
        document::PageTables tables;
        if (!table_.recognize(pages[index], page_texts[index], page_layouts[index], tables)) {
            return common::Status::error("table.recognition_failed",
                                         "table recognition failed for page " + std::to_string(index + 1));
        }
        page_tables.push_back(tables);
    }

    return common::Status::ok();
}

} // namespace doc_parser::pipeline
