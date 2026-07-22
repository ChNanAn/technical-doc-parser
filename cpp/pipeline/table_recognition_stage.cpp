#include "pipeline/table_recognition_stage.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace doc_parser::pipeline {
namespace {

double bboxArea(const document::BBox& bbox) {
    return std::max(0.0, bbox.x1 - bbox.x0) * std::max(0.0, bbox.y1 - bbox.y0);
}

double lineCoverage(const document::BBox& line, const document::BBox& table) {
    const double width = std::max(0.0, std::min(line.x1, table.x1) - std::max(line.x0, table.x0));
    const double height = std::max(0.0, std::min(line.y1, table.y1) - std::max(line.y0, table.y0));
    const double area = bboxArea(line);
    return area <= 0.0 ? 0.0 : width * height / area;
}

void attachDetectedTables(const document::PageText& text, document::PageLayout& layout, document::PageTables& tables) {
    std::set<std::string> assigned_layout_blocks;
    for (document::Table& table : tables.tables) {
        const auto existing = std::find_if(layout.blocks.begin(), layout.blocks.end(), [&](const auto& block) {
            return !table.layout_block_id.empty() && block.id == table.layout_block_id;
        });
        if (existing != layout.blocks.end() && assigned_layout_blocks.insert(existing->id).second) {
            continue;
        }

        document::LayoutBlock block;
        block.id = "page_" + std::to_string(layout.page_number) + "_block_" + std::to_string(layout.blocks.size() + 1);
        block.type = document::LayoutBlockType::Table;
        block.source_label = table.source_label.empty() ? "table detector" : table.source_label;
        block.bbox = table.bbox;
        block.confidence = table.confidence;
        for (std::size_t line_index = 0; line_index < text.lines.size(); ++line_index) {
            if (lineCoverage(text.lines[line_index].bbox, block.bbox) >= 0.5) {
                block.text_line_indices.push_back(static_cast<int>(line_index));
            }
        }
        table.layout_block_id = block.id;
        assigned_layout_blocks.insert(block.id);
        layout.blocks.push_back(std::move(block));
    }
}

bool continuationCandidate(const document::Table& previous,
                           const document::PageArtifact& previous_page,
                           const document::Table& next,
                           const document::PageArtifact& next_page) {
    if (previous_page.height <= 0 || next_page.height <= 0 || previous_page.width <= 0 || next_page.width <= 0) {
        return false;
    }
    const bool touches_bottom = previous.bbox.y1 >= previous_page.height * 0.82;
    const bool touches_top = next.bbox.y0 <= next_page.height * 0.18;
    const double previous_left = previous.bbox.x0 / previous_page.width;
    const double next_left = next.bbox.x0 / next_page.width;
    const double previous_right = previous.bbox.x1 / previous_page.width;
    const double next_right = next.bbox.x1 / next_page.width;
    const bool aligned = std::abs(previous_left - next_left) <= 0.08 && std::abs(previous_right - next_right) <= 0.08;
    const bool compatible_columns = previous.columns.empty() || next.columns.empty() ||
                                    previous.columns.size() == next.columns.size();
    return touches_bottom && touches_top && aligned && compatible_columns;
}

void linkCrossPageTables(const std::vector<document::PageArtifact>& pages,
                         std::vector<document::PageTables>& page_tables) {
    for (std::size_t page_index = 1; page_index < page_tables.size(); ++page_index) {
        for (std::size_t previous_index = 0; previous_index < page_tables[page_index - 1].tables.size();
             ++previous_index) {
            document::Table& previous = page_tables[page_index - 1].tables[previous_index];
            for (document::Table& next : page_tables[page_index].tables) {
                if (!continuationCandidate(previous, pages[page_index - 1], next, pages[page_index])) {
                    continue;
                }
                if (previous.continuation_group_id.empty()) {
                    previous.continuation_group_id = "table_group_page_" + std::to_string(previous.page_number) + "_" +
                                                     std::to_string(previous_index + 1);
                }
                previous.continues_on_next_page = true;
                next.continuation_group_id = previous.continuation_group_id;
                next.continues_from_previous_page = true;
                break;
            }
        }
    }
}

} // namespace

TableRecognitionStage::TableRecognitionStage(const table::ITableBackend& table) : table_(table) {}

common::Status TableRecognitionStage::recognize(const PipelineContext& context,
                                                const std::vector<document::PageArtifact>& pages,
                                                const std::vector<document::PageText>& page_texts,
                                                std::vector<document::PageLayout>& page_layouts,
                                                std::vector<document::PageTables>& page_tables) const {
    (void)context;
    page_tables.clear();

    if (pages.size() != page_texts.size() || pages.size() != page_layouts.size()) {
        return common::Status::error("table.page_count_mismatch",
                                     "page, text, and layout counts must match before table recognition");
    }

    page_tables.reserve(pages.size());
    for (std::size_t index = 0; index < pages.size(); ++index) {
        table::TableResult result;
        if (!table_.recognize({pages[index], page_texts[index], page_layouts[index]}, result)) {
            return common::Status::error("table.recognition_failed",
                                         "table recognition failed for page " + std::to_string(index + 1));
        }
        attachDetectedTables(page_texts[index], page_layouts[index], result.tables);
        page_tables.push_back(result.tables);
    }

    linkCrossPageTables(pages, page_tables);

    return common::Status::ok();
}

} // namespace doc_parser::pipeline
