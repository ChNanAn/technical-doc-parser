#pragma once

#include <string>
#include <vector>

namespace doc_parser::document {

struct ReadingOrderItem {
    std::string layout_block_id;
    int layout_block_index = -1;
    int sequence_index = 0;
};

struct PageReadingOrder {
    int page_index = 0;
    int page_number = 0;
    std::vector<ReadingOrderItem> items;
};

} // namespace doc_parser::document
