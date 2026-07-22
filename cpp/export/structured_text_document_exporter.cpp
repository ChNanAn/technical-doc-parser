#include "export/structured_text_document_exporter.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace doc_parser::exporter {
namespace {

std::string htmlEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

std::string markdownEscape(std::string value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        if (character == '|') {
            escaped += "\\|";
        } else if (character == '\n' || character == '\r') {
            escaped += "<br>";
        } else {
            escaped += character;
        }
    }
    return escaped;
}

bool isTableBlock(const document::DocumentBlock& block) {
    return block.type == document::DocumentBlockType::Table && !block.table_id.empty();
}

bool sameLogicalTable(const document::DocumentBlock& first, const document::DocumentBlock& candidate) {
    return !first.table_continuation_group_id.empty() &&
           first.table_continuation_group_id == candidate.table_continuation_group_id;
}

std::vector<document::TableRow> logicalRows(const std::vector<document::DocumentBlock>& blocks,
                                            std::size_t first_index) {
    std::vector<document::TableRow> rows;
    const document::DocumentBlock& first = blocks[first_index];
    for (std::size_t index = first_index; index < blocks.size(); ++index) {
        const document::DocumentBlock& block = blocks[index];
        if (index != first_index && !sameLogicalTable(first, block)) {
            continue;
        }
        std::size_t row_begin = 0;
        if (!rows.empty() && block.table_continues_from_previous_page) {
            while (row_begin < block.table_rows.size() && block.table_rows[row_begin].is_header) {
                ++row_begin;
            }
        }
        for (std::size_t row_index = row_begin; row_index < block.table_rows.size(); ++row_index) {
            document::TableRow row = block.table_rows[row_index];
            row.row_index = static_cast<int>(rows.size());
            for (document::TableCell& cell : row.cells) {
                cell.row_index = row.row_index;
            }
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

bool hasMergedCells(const std::vector<document::TableRow>& rows) {
    for (const document::TableRow& row : rows) {
        if (std::any_of(row.cells.begin(), row.cells.end(), [](const auto& cell) {
                return cell.row_span > 1 || cell.column_span > 1;
            })) {
            return true;
        }
    }
    return false;
}

int columnCount(const std::vector<document::TableRow>& rows) {
    int count = 0;
    for (const document::TableRow& row : rows) {
        for (const document::TableCell& cell : row.cells) {
            count = std::max(count, cell.column_index + std::max(1, cell.column_span));
        }
    }
    return count;
}

std::string htmlTable(const std::vector<document::TableRow>& rows) {
    std::ostringstream output;
    output << "<table>\n";
    bool header_open = false;
    bool body_open = false;
    for (const document::TableRow& row : rows) {
        if (row.is_header && !body_open && !header_open) {
            output << "  <thead>\n";
            header_open = true;
        } else if (!row.is_header && !body_open) {
            if (header_open) {
                output << "  </thead>\n";
                header_open = false;
            }
            output << "  <tbody>\n";
            body_open = true;
        }
        output << "    <tr>\n";
        for (const document::TableCell& cell : row.cells) {
            const bool header = row.is_header || cell.is_header;
            output << "      <" << (header ? "th" : "td");
            if (cell.row_span > 1) {
                output << " rowspan=\"" << cell.row_span << "\"";
            }
            if (cell.column_span > 1) {
                output << " colspan=\"" << cell.column_span << "\"";
            }
            output << '>' << htmlEscape(cell.text) << "</" << (header ? "th" : "td") << ">\n";
        }
        output << "    </tr>\n";
    }
    if (header_open) {
        output << "  </thead>\n";
    }
    if (body_open) {
        output << "  </tbody>\n";
    }
    output << "</table>";
    return output.str();
}

std::vector<std::string> flattenedRow(const document::TableRow& row, int columns) {
    std::vector<std::string> values(static_cast<std::size_t>(columns));
    for (const document::TableCell& cell : row.cells) {
        if (cell.column_index >= 0 && cell.column_index < columns) {
            values[static_cast<std::size_t>(cell.column_index)] = markdownEscape(cell.text);
        }
    }
    return values;
}

void writeMarkdownTable(std::ostream& output, const std::vector<document::TableRow>& rows) {
    if (rows.empty()) {
        return;
    }
    if (hasMergedCells(rows)) {
        output << htmlTable(rows) << "\n\n";
        return;
    }
    const int columns = columnCount(rows);
    if (columns <= 0) {
        return;
    }
    const std::vector<std::string> header = flattenedRow(rows.front(), columns);
    output << '|';
    for (const std::string& value : header) {
        output << ' ' << value << " |";
    }
    output << "\n|";
    for (int column = 0; column < columns; ++column) {
        output << " --- |";
    }
    output << '\n';
    for (std::size_t row_index = 1; row_index < rows.size(); ++row_index) {
        output << '|';
        for (const std::string& value : flattenedRow(rows[row_index], columns)) {
            output << ' ' << value << " |";
        }
        output << '\n';
    }
    output << '\n';
}

bool isContinuationAlreadyWritten(const std::vector<document::DocumentBlock>& blocks, std::size_t index) {
    const document::DocumentBlock& block = blocks[index];
    if (block.table_continuation_group_id.empty()) {
        return false;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
        if (sameLogicalTable(block, blocks[previous])) {
            return true;
        }
    }
    return false;
}

} // namespace

bool MarkdownDocumentExporter::write(const DocumentExportRequest& request) const {
    if (request.document == nullptr || request.output_path.empty()) {
        return false;
    }
    std::ofstream output(request.output_path);
    if (!output) {
        return false;
    }
    const auto& blocks = request.document->blocks;
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        const document::DocumentBlock& block = blocks[index];
        if (isTableBlock(block)) {
            if (!isContinuationAlreadyWritten(blocks, index)) {
                writeMarkdownTable(output, logicalRows(blocks, index));
            }
            continue;
        }
        if (block.text.empty()) {
            continue;
        }
        if (block.type == document::DocumentBlockType::Title) {
            output << "## " << block.text << "\n\n";
        } else if (block.type == document::DocumentBlockType::List) {
            output << "- " << block.text << "\n";
        } else if (block.type != document::DocumentBlockType::Header &&
                   block.type != document::DocumentBlockType::Footer) {
            output << block.text << "\n\n";
        }
    }
    return static_cast<bool>(output);
}

bool HtmlDocumentExporter::write(const DocumentExportRequest& request) const {
    if (request.document == nullptr || request.output_path.empty()) {
        return false;
    }
    std::ofstream output(request.output_path);
    if (!output) {
        return false;
    }
    output << "<!doctype html>\n<html><head><meta charset=\"utf-8\"><title>"
           << htmlEscape(request.document->source.path) << "</title></head><body>\n";
    const auto& blocks = request.document->blocks;
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        const document::DocumentBlock& block = blocks[index];
        if (isTableBlock(block)) {
            if (!isContinuationAlreadyWritten(blocks, index)) {
                output << htmlTable(logicalRows(blocks, index)) << "\n";
            }
            continue;
        }
        if (block.text.empty()) {
            continue;
        }
        const char* tag = block.type == document::DocumentBlockType::Title ? "h2" : "p";
        output << '<' << tag << '>' << htmlEscape(block.text) << "</" << tag << ">\n";
    }
    output << "</body></html>\n";
    return static_cast<bool>(output);
}

} // namespace doc_parser::exporter
