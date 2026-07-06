#include "pipeline/table_recognition_stage.h"

namespace doc_parser::pipeline {

TableRecognitionStage::TableRecognitionStage(const table::TableService& table) : table_(table) {}

bool TableRecognitionStage::recognize(const PipelineContext& context,
                                      const std::vector<document::PageArtifact>& pages,
                                      const std::vector<document::PageText>& page_texts,
                                      const std::vector<document::PageLayout>& page_layouts,
                                      std::vector<document::PageTables>& page_tables) const {
    (void)context;
    page_tables.clear();

    if (pages.size() != page_texts.size() || pages.size() != page_layouts.size()) {
        return false;
    }

    page_tables.reserve(pages.size());
    for (std::size_t index = 0; index < pages.size(); ++index) {
        document::PageTables tables;
        if (!table_.recognize(pages[index], page_texts[index], page_layouts[index], tables)) {
            return false;
        }
        page_tables.push_back(tables);
    }

    return true;
}

} // namespace doc_parser::pipeline
