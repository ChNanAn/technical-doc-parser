#pragma once

#include "document/table_model.h"
#include "document/text_model.h"

#include <string>
#include <vector>

namespace doc_parser::document {

enum class DocumentBlockType {
    Unknown,
    Title,
    Paragraph,
    List,
    Table,
    Figure,
    Header,
    Footer,
};

struct DocumentBlock {
    std::string id;
    DocumentBlockType type = DocumentBlockType::Unknown;
    int page_index = 0;
    int page_number = 0;
    BBox bbox;
    double confidence = 1.0;
    std::string text;
    std::vector<int> text_line_indices;
    std::string table_id;
    std::vector<TableRow> table_rows;
};

} // namespace doc_parser::document
