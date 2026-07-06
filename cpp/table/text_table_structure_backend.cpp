#include "table/table_backend.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace doc_parser::table {
namespace {

using document::BBox;
using document::Table;
using document::TableCell;
using document::TableRow;

double bboxHeight(const BBox& bbox) { return std::max(0.0, bbox.y1 - bbox.y0); }

void expandBBox(BBox& target, const BBox& value) {
    target.x0 = std::min(target.x0, value.x0);
    target.y0 = std::min(target.y0, value.y0);
    target.x1 = std::max(target.x1, value.x1);
    target.y1 = std::max(target.y1, value.y1);
}

std::string trim(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); });
    return std::string(first, last.base());
}

bool isDotLeader(const std::string& text) {
    const std::string value = trim(text);
    if (value.size() < 3) {
        return false;
    }

    int leader_chars = 0;
    for (const char c : value) {
        if (c == '.' || c == '_' || c == '-' || c == ' ') {
            ++leader_chars;
        }
    }
    return static_cast<double>(leader_chars) / static_cast<double>(value.size()) >= 0.75;
}

std::string tableId(int page_number, std::size_t table_index) {
    std::ostringstream stream;
    stream << "page_" << page_number << "_table_" << table_index + 1;
    return stream.str();
}

std::string joinCellText(const std::vector<document::TextSpan>& spans) {
    std::string text;
    for (const auto& span : spans) {
        const std::string value = trim(span.text);
        if (value.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += ' ';
        }
        text += value;
    }
    return text;
}

TableCell makeCell(const std::vector<document::TextSpan>& spans, int row_index, int column_index) {
    TableCell cell;
    cell.row_index = row_index;
    cell.column_index = column_index;
    cell.text = joinCellText(spans);
    cell.confidence = 0.0;

    if (spans.empty()) {
        return cell;
    }

    cell.bbox = spans.front().bbox;
    for (const auto& span : spans) {
        expandBBox(cell.bbox, span.bbox);
        cell.confidence += span.confidence;
    }
    cell.confidence /= static_cast<double>(spans.size());
    return cell;
}

std::vector<TableCell> cellsFromLine(const document::TextLine& line, int row_index) {
    std::vector<document::TextSpan> spans;
    spans.reserve(line.spans.size());
    for (const auto& span : line.spans) {
        if (!trim(span.text).empty() && !isDotLeader(span.text)) {
            spans.push_back(span);
        }
    }

    std::sort(spans.begin(), spans.end(), [](const auto& lhs, const auto& rhs) { return lhs.bbox.x0 < rhs.bbox.x0; });

    if (spans.empty()) {
        TableCell cell;
        cell.row_index = row_index;
        cell.column_index = 0;
        cell.text = trim(line.text);
        cell.bbox = line.bbox;
        cell.confidence = line.confidence;
        return {cell};
    }

    const double gap_threshold = std::max(24.0, bboxHeight(line.bbox) * 2.0);
    std::vector<TableCell> cells;
    std::vector<document::TextSpan> current_cell;

    for (const auto& span : spans) {
        if (!current_cell.empty()) {
            const double gap = span.bbox.x0 - current_cell.back().bbox.x1;
            if (gap > gap_threshold) {
                cells.push_back(makeCell(current_cell, row_index, static_cast<int>(cells.size())));
                current_cell.clear();
            }
        }
        current_cell.push_back(span);
    }

    if (!current_cell.empty()) {
        cells.push_back(makeCell(current_cell, row_index, static_cast<int>(cells.size())));
    }
    return cells;
}

bool hasUsableTableShape(const Table& table) {
    if (table.rows.empty()) {
        return false;
    }
    if (table.rows.size() >= 2) {
        return true;
    }
    return !table.rows.front().cells.empty();
}

} // namespace

bool TextTableStructureBackend::recognize(const TableRequest& request, TableResult& result) const {
    result.tables = {};
    result.tables.page_index = request.page.page_index;
    result.tables.page_number = request.page.page_number;

    for (const auto& block : request.layout.blocks) {
        if (block.type != document::LayoutBlockType::Table) {
            continue;
        }

        Table table;
        table.id = tableId(request.page.page_number, result.tables.tables.size());
        table.layout_block_id = block.id;
        table.page_index = request.page.page_index;
        table.page_number = request.page.page_number;
        table.bbox = block.bbox;
        table.confidence = block.confidence;

        for (const int line_index : block.text_line_indices) {
            if (line_index < 0 || static_cast<std::size_t>(line_index) >= request.text.lines.size()) {
                continue;
            }

            const auto& line = request.text.lines[static_cast<std::size_t>(line_index)];
            TableRow row;
            row.row_index = static_cast<int>(table.rows.size());
            row.cells = cellsFromLine(line, row.row_index);
            if (!row.cells.empty()) {
                table.rows.push_back(row);
            }
        }

        if (hasUsableTableShape(table)) {
            result.tables.tables.push_back(table);
        }
    }

    return true;
}

} // namespace doc_parser::table
