#pragma once

#include "document/text_model.h"

#include <string>
#include <vector>

namespace doc_parser::document {

struct TableCell {
    int row_index = 0;
    int column_index = 0;
    int row_span = 1;
    int column_span = 1;
    std::string text;
    BBox bbox;
    double confidence = 1.0;
};

struct TableRow {
    int row_index = 0;
    std::vector<TableCell> cells;
};

struct Table {
    std::string id;
    std::string layout_block_id;
    int page_index = 0;
    int page_number = 0;
    BBox bbox;
    double confidence = 1.0;
    std::vector<TableRow> rows;
};

struct PageTables {
    int page_index = 0;
    int page_number = 0;
    std::vector<Table> tables;
};

} // namespace doc_parser::document
