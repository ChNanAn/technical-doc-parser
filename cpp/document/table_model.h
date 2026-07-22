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
    bool is_header = false;
    std::string text;
    BBox bbox;
    double confidence = 1.0;
};

struct TableRow {
    int row_index = 0;
    BBox bbox;
    double confidence = 1.0;
    bool is_header = false;
    std::vector<TableCell> cells;
};

struct TableColumn {
    int column_index = 0;
    BBox bbox;
    double confidence = 1.0;
};

struct TableStructureObject {
    std::string label;
    BBox bbox;
    double confidence = 1.0;
};

struct Table {
    std::string id;
    std::string layout_block_id;
    int page_index = 0;
    int page_number = 0;
    BBox bbox;
    double confidence = 1.0;
    std::string source_label;
    std::string continuation_group_id;
    bool continues_from_previous_page = false;
    bool continues_on_next_page = false;
    std::vector<TableColumn> columns;
    std::vector<TableRow> rows;
    std::vector<TableStructureObject> structure_objects;
};

struct PageTables {
    int page_index = 0;
    int page_number = 0;
    std::vector<Table> tables;
};

} // namespace doc_parser::document
