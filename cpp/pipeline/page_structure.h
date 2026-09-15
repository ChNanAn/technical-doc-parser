#pragma once

#include "document/layout_model.h"
#include "document/table_model.h"

namespace doc_parser::pipeline {

struct PageStructureStats {
    int merged_figures = 0;
    int preserved_unique_lines = 0;
    int source_text_tables = 0;
};

// Reconcile near-identical visual detections and choose table reading text before
// layout block indices are consumed by the reading-order stage.
PageStructureStats
resolvePageStructure(const document::PageText& text, document::PageLayout& layout, document::PageTables& tables);

} // namespace doc_parser::pipeline
