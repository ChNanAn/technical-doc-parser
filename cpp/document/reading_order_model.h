#pragma once

#include <map>
#include <string>
#include <vector>

namespace doc_parser::document {

struct ReadingOrderPlacement {
    std::string layout_block_id;
    std::string group;
    int band_index = 0;
    int column_start = 0;
    int column_end = 0;
};

struct ReadingOrderCycleBreak {
    std::string from_layout_block_id;
    std::string to_layout_block_id;
    std::string reason;
    double confidence = 0.0;
};

struct ReadingOrderTrace {
    std::string algorithm;
    std::vector<ReadingOrderPlacement> placements;
    std::map<std::string, int> edge_counts;
    std::vector<ReadingOrderCycleBreak> cycle_breaks;
};

struct ReadingOrderItem {
    std::string layout_block_id;
    int layout_block_index = -1;
    int sequence_index = 0;
};

struct PageReadingOrder {
    int page_index = 0;
    int page_number = 0;
    std::vector<ReadingOrderItem> items;
    ReadingOrderTrace trace;
};

} // namespace doc_parser::document
