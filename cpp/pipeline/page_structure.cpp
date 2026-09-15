#include "pipeline/page_structure.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <spdlog/spdlog.h>

namespace doc_parser::pipeline {
namespace {

bool validBox(const document::BBox& box) {
    return std::isfinite(box.x0) && std::isfinite(box.y0) && std::isfinite(box.x1) && std::isfinite(box.y1) &&
           box.x1 > box.x0 && box.y1 > box.y0;
}

double area(const document::BBox& box) { return (box.x1 - box.x0) * (box.y1 - box.y0); }

void expand(document::BBox& box, const document::BBox& other) {
    box.x0 = std::min(box.x0, other.x0);
    box.y0 = std::min(box.y0, other.y0);
    box.x1 = std::max(box.x1, other.x1);
    box.y1 = std::max(box.y1, other.y1);
}

bool sameVisualRegion(const document::LayoutBlock& table, const document::LayoutBlock& figure) {
    if (!validBox(table.bbox) || !validBox(figure.bbox)) {
        return false;
    }
    const double intersection =
        std::max(0.0, std::min(table.bbox.x1, figure.bbox.x1) - std::max(table.bbox.x0, figure.bbox.x0)) *
        std::max(0.0, std::min(table.bbox.y1, figure.bbox.y1) - std::max(table.bbox.y0, figure.bbox.y0));
    return intersection / (area(table.bbox) + area(figure.bbox) - intersection) >= 0.5;
}

std::set<int> sourceLines(const document::LayoutBlock& block, const document::PageText& text) {
    std::set<int> lines;
    for (const int index : block.text_line_indices) {
        if (index < 0 || static_cast<std::size_t>(index) >= text.lines.size() ||
            !validBox(text.lines[static_cast<std::size_t>(index)].bbox)) {
            return {};
        }
        lines.insert(index);
    }
    return lines;
}

bool sameLineOwnership(const std::set<int>& table, const std::set<int>& figure) {
    if (table.empty() || figure.empty()) {
        return false;
    }
    const auto shared = std::count_if(figure.begin(), figure.end(), [&](int index) { return table.count(index); });
    return static_cast<double>(shared) / table.size() >= 0.8 && static_cast<double>(shared) / figure.size() >= 0.8;
}

bool structureLosesSourceText(const document::LayoutBlock& block,
                              const document::Table& table,
                              const document::PageText& text) {
    std::size_t source_bytes = 0;
    for (const int index : block.text_line_indices) {
        if (index >= 0 && static_cast<std::size_t>(index) < text.lines.size()) {
            if (source_bytes > 0) {
                ++source_bytes;
            }
            source_bytes += text.lines[static_cast<std::size_t>(index)].text.size();
        }
    }
    if (source_bytes == 0) {
        return false;
    }
    std::size_t cell_bytes = 0;
    bool has_cell_text = false;
    for (const auto& row : table.rows) {
        if (cell_bytes > 0) {
            ++cell_bytes;
        }
        bool first_cell = true;
        for (const auto& cell : row.cells) {
            cell_bytes += cell.text.size() + (first_cell ? 0 : 1);
            first_cell = false;
            has_cell_text = has_cell_text || cell.text.find_first_not_of(" \t\r\n") != std::string::npos;
        }
    }
    // Retain the existing form fallback policy, and preserve source text when
    // recognition produced an empty grid. No geometry or cell content is changed.
    return !has_cell_text || (block.text_line_indices.size() / 2 >= table.rows.size() &&
                              static_cast<double>(cell_bytes) < static_cast<double>(source_bytes) * 0.9);
}

} // namespace

PageStructureStats
resolvePageStructure(const document::PageText& text, document::PageLayout& layout, document::PageTables& tables) {
    PageStructureStats stats;
    std::map<std::string, std::size_t> blocks_by_id;
    for (std::size_t index = 0; index < layout.blocks.size(); ++index) {
        blocks_by_id.emplace(layout.blocks[index].id, index);
    }
    std::map<std::size_t, document::Table*> table_blocks;
    for (auto& table : tables.tables) {
        const auto block = blocks_by_id.find(table.layout_block_id);
        if (block != blocks_by_id.end() && layout.blocks[block->second].type == document::LayoutBlockType::Table) {
            table_blocks.emplace(block->second, &table);
        }
    }
    std::vector<std::set<int>> lines;
    for (const auto& block : layout.blocks) {
        lines.push_back(sourceLines(block, text));
    }
    // Decide against the unmodified layout. Multiple plausible table owners are
    // ambiguous, so retain that figure instead of resolving by vector order.
    std::map<std::size_t, std::size_t> replacements;
    for (std::size_t index = 0; index < layout.blocks.size(); ++index) {
        const auto& figure = layout.blocks[index];
        if (figure.type != document::LayoutBlockType::Figure) {
            continue;
        }
        std::vector<std::size_t> candidates;
        for (const auto& [table_index, table] : table_blocks) {
            (void)table;
            if (sameVisualRegion(layout.blocks[table_index], figure) &&
                sameLineOwnership(lines[table_index], lines[index])) {
                candidates.push_back(table_index);
            }
        }
        if (candidates.size() == 1) {
            replacements.emplace(index, candidates.front());
        }
    }
    std::map<std::string, std::string> remapped_ids;
    for (const auto& [figure_index, table_index] : replacements) {
        const auto& figure = layout.blocks[figure_index];
        auto& block = layout.blocks[table_index];
        for (int line : lines[figure_index]) {
            if (lines[table_index].insert(line).second) {
                ++stats.preserved_unique_lines;
            }
        }
        block.text_line_indices.assign(lines[table_index].begin(), lines[table_index].end());
        for (int line : block.text_line_indices) {
            expand(block.bbox, text.lines[static_cast<std::size_t>(line)].bbox);
        }
        block.confidence = std::min(block.confidence, figure.confidence);
        table_blocks.at(table_index)->text_mode = document::TableTextMode::SourceLines;
        remapped_ids.emplace(figure.id, block.id);
        ++stats.merged_figures;
        spdlog::debug("page_structure: page={} merged_figure={} table={} source_lines={}",
                      layout.page_number,
                      figure.id,
                      block.id,
                      block.text_line_indices.size());
    }
    for (const auto& [index, table] : table_blocks) {
        if (structureLosesSourceText(layout.blocks[index], *table, text)) {
            table->text_mode = document::TableTextMode::SourceLines;
        }
        stats.source_text_tables += table->text_mode == document::TableTextMode::SourceLines;
    }
    for (auto& block : layout.blocks) {
        const auto replacement = remapped_ids.find(block.related_block_id);
        if (replacement != remapped_ids.end()) {
            block.related_block_id = replacement->second == block.id ? "" : replacement->second;
        }
    }
    std::vector<document::LayoutBlock> resolved;
    resolved.reserve(layout.blocks.size() - replacements.size());
    for (std::size_t index = 0; index < layout.blocks.size(); ++index) {
        if (!replacements.count(index)) {
            resolved.push_back(std::move(layout.blocks[index]));
        }
    }
    layout.blocks = std::move(resolved);
    return stats;
}

} // namespace doc_parser::pipeline
