#pragma once

#include "document/text_model.h"

#include <string>
#include <vector>

namespace doc_parser::document {

enum class LayoutBlockType {
    Unknown,
    Title,
    Text,
    List,
    Table,
    Figure,
    Header,
    Footer,
};

struct LayoutBlock {
    std::string id;
    LayoutBlockType type = LayoutBlockType::Unknown;
    BBox bbox;
    double confidence = 1.0;
    std::vector<int> text_line_indices;
};

struct PageLayout {
    int page_index = 0;
    int page_number = 0;
    std::vector<LayoutBlock> blocks;
};

} // namespace doc_parser::document
