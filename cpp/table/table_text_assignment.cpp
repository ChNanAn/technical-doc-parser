#include "table/table_text_assignment.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace doc_parser::table::detail {
namespace {

bool validBox(const document::BBox& bbox) {
    return std::isfinite(bbox.x0) && std::isfinite(bbox.y0) && std::isfinite(bbox.x1) && std::isfinite(bbox.y1) &&
           bbox.x1 > bbox.x0 && bbox.y1 > bbox.y0;
}

double centerX(const document::BBox& bbox) { return bbox.x0 * 0.5 + bbox.x1 * 0.5; }

double centerY(const document::BBox& bbox) { return bbox.y0 * 0.5 + bbox.y1 * 0.5; }

bool centerInside(const document::BBox& outer, const document::BBox& inner) {
    return centerX(inner) >= outer.x0 && centerX(inner) <= outer.x1 && centerY(inner) >= outer.y0 &&
           centerY(inner) <= outer.y1;
}

double intersectionArea(const document::BBox& lhs, const document::BBox& rhs) {
    return std::max(0.0, std::min(lhs.x1, rhs.x1) - std::max(lhs.x0, rhs.x0)) *
           std::max(0.0, std::min(lhs.y1, rhs.y1) - std::max(lhs.y0, rhs.y0));
}

double centerDistance(const document::BBox& cell, const document::BBox& token) {
    const double x = (centerX(token) - centerX(cell)) / (cell.x1 - cell.x0);
    const double y = (centerY(token) - centerY(cell)) / (cell.y1 - cell.y0);
    return x * x + y * y;
}

void fillCell(document::TableCell& cell, std::vector<const TableTextToken*>& matches) {
    // A tolerance inside a sort comparator is not transitive. Sort vertically
    // first, then sort bounded baseline groups from left to right.
    std::stable_sort(matches.begin(), matches.end(), [](const auto* lhs, const auto* rhs) {
        return centerY(lhs->bbox) < centerY(rhs->bbox);
    });
    for (auto first = matches.begin(); first != matches.end();) {
        auto last = first + 1;
        while (last != matches.end() && centerY((*last)->bbox) - centerY((*first)->bbox) <= 3.0) {
            ++last;
        }
        std::stable_sort(first, last, [](const auto* lhs, const auto* rhs) { return lhs->bbox.x0 < rhs->bbox.x0; });
        first = last;
    }

    cell.text.clear();
    double confidence = 0.0;
    for (const auto* token : matches) {
        if (!cell.text.empty()) {
            cell.text += ' ';
        }
        cell.text += token->text;
        confidence += token->confidence;
    }
    if (!matches.empty()) {
        cell.confidence = std::min(cell.confidence, confidence / static_cast<double>(matches.size()));
    }
}

} // namespace

std::vector<TableTextToken> collectTableTextTokens(const document::PageText& text) {
    std::vector<TableTextToken> tokens;
    for (const document::TextLine& line : text.lines) {
        if (line.spans.empty()) {
            if (!line.text.empty()) {
                tokens.push_back({line.text, line.bbox, line.confidence});
            }
        } else {
            for (const document::TextSpan& span : line.spans) {
                if (!span.text.empty()) {
                    tokens.push_back({span.text, span.bbox, span.confidence});
                }
            }
        }
    }
    return tokens;
}

TableTextAssignmentStats assignTableText(document::Table& table, const std::vector<TableTextToken>& tokens) {
    std::vector<document::TableCell*> cells;
    for (auto& row : table.rows) {
        for (auto& cell : row.cells) {
            cells.push_back(&cell);
        }
    }
    std::vector<std::vector<const TableTextToken*>> matches(cells.size());
    TableTextAssignmentStats stats;
    for (const auto& token : tokens) {
        if (token.text.empty() || !validBox(token.bbox)) {
            continue;
        }
        std::size_t best = cells.size();
        std::size_t candidates = 0;
        std::tuple<double, double, int, int> best_rank;
        for (std::size_t index = 0; index < cells.size(); ++index) {
            const auto& cell = *cells[index];
            if (!validBox(cell.bbox) || !centerInside(cell.bbox, token.bbox)) {
                continue;
            }
            ++candidates;
            // All candidates refer to the same token, so maximizing intersection
            // area also maximizes its coverage. Resolve full-coverage ties by
            // proximity, then logical cell position, independent of grid storage.
            const auto rank = std::make_tuple(-intersectionArea(cell.bbox, token.bbox),
                                              centerDistance(cell.bbox, token.bbox),
                                              cell.row_index,
                                              cell.column_index);
            if (best == cells.size() || rank < best_rank) {
                best = index;
                best_rank = rank;
            }
        }
        if (best != cells.size()) {
            matches[best].push_back(&token);
            ++stats.assigned_tokens;
            stats.ambiguous_tokens += candidates > 1;
        } else if (validBox(table.bbox) && centerInside(table.bbox, token.bbox)) {
            ++stats.unassigned_tokens;
        }
    }
    for (std::size_t index = 0; index < cells.size(); ++index) {
        fillCell(*cells[index], matches[index]);
    }
    return stats;
}

} // namespace doc_parser::table::detail
