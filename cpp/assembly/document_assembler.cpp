#include "assembly/document_assembler.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace doc_parser::assembly {
namespace {

document::DocumentBlockType toDocumentBlockType(document::LayoutBlockType type) {
    switch (type) {
    case document::LayoutBlockType::Title:
        return document::DocumentBlockType::Title;
    case document::LayoutBlockType::Text:
        return document::DocumentBlockType::Paragraph;
    case document::LayoutBlockType::List:
        return document::DocumentBlockType::List;
    case document::LayoutBlockType::Table:
        return document::DocumentBlockType::Table;
    case document::LayoutBlockType::Figure:
        return document::DocumentBlockType::Figure;
    case document::LayoutBlockType::Header:
        return document::DocumentBlockType::Header;
    case document::LayoutBlockType::Footer:
        return document::DocumentBlockType::Footer;
    case document::LayoutBlockType::Unknown:
        return document::DocumentBlockType::Unknown;
    }
    return document::DocumentBlockType::Unknown;
}

std::string blockId(int page_number, std::size_t block_index) {
    std::ostringstream stream;
    stream << "doc_page_" << page_number << "_block_" << block_index + 1;
    return stream.str();
}

std::string joinLayoutBlockText(const document::PageText& page_text, const document::LayoutBlock& layout_block) {
    std::string text;
    for (const int line_index : layout_block.text_line_indices) {
        if (line_index < 0 || static_cast<std::size_t>(line_index) >= page_text.lines.size()) {
            continue;
        }

        if (!text.empty()) {
            text += '\n';
        }
        text += page_text.lines[static_cast<std::size_t>(line_index)].text;
    }
    return text;
}

const document::Table* findTableForLayoutBlock(const document::PageTables& page_tables,
                                               const std::string& layout_block_id) {
    const auto table = std::find_if(page_tables.tables.begin(), page_tables.tables.end(), [&](const auto& value) {
        return value.layout_block_id == layout_block_id;
    });
    if (table == page_tables.tables.end()) {
        return nullptr;
    }
    return &(*table);
}

std::string tableText(const document::Table& table) {
    std::string text;
    for (const auto& row : table.rows) {
        if (!text.empty()) {
            text += '\n';
        }

        bool first_cell = true;
        for (const auto& cell : row.cells) {
            if (!first_cell) {
                text += '\t';
            }
            text += cell.text;
            first_cell = false;
        }
    }
    return text;
}

document::DocumentBlock makeDocumentBlock(const document::PipelinePageArtifacts& page,
                                          const document::LayoutBlock& layout_block,
                                          std::string document_block_id,
                                          const std::map<std::string, std::string>& related_block_ids) {
    document::DocumentBlock block;
    block.id = std::move(document_block_id);
    block.type = toDocumentBlockType(layout_block.type);
    block.source_label = layout_block.source_label;
    const auto related = related_block_ids.find(layout_block.related_block_id);
    if (related != related_block_ids.end()) {
        block.related_block_id = related->second;
    }
    block.page_index = page.page_index;
    block.page_number = page.page_number;
    block.bbox = layout_block.bbox;
    block.confidence = layout_block.confidence;
    block.text_line_indices = layout_block.text_line_indices;
    block.text = joinLayoutBlockText(page.text, layout_block);

    if (block.type == document::DocumentBlockType::Table) {
        const document::Table* table = findTableForLayoutBlock(page.tables, layout_block.id);
        if (table != nullptr) {
            block.table_id = table->id;
            block.table_continuation_group_id = table->continuation_group_id;
            block.table_continues_from_previous_page = table->continues_from_previous_page;
            block.table_continues_on_next_page = table->continues_on_next_page;
            block.table_rows = table->rows;
            block.text = tableText(*table);
            block.confidence = std::min(block.confidence, table->confidence);
        }
    }

    return block;
}

bool hasMatchingPageCounts(const DocumentAssembleRequest& request) {
    return request.pages.size() == request.page_texts.size() && request.pages.size() == request.page_layouts.size() &&
           request.pages.size() == request.page_reading_orders.size() &&
           request.pages.size() == request.page_tables.size();
}

std::vector<int> orderedLayoutBlockIndices(const document::PipelinePageArtifacts& page) {
    std::vector<int> indices;
    std::set<int> seen;

    for (const auto& item : page.reading_order.items) {
        if (item.layout_block_index < 0 ||
            static_cast<std::size_t>(item.layout_block_index) >= page.layout.blocks.size()) {
            continue;
        }
        if (!item.layout_block_id.empty() &&
            page.layout.blocks[static_cast<std::size_t>(item.layout_block_index)].id != item.layout_block_id) {
            continue;
        }
        if (seen.insert(item.layout_block_index).second) {
            indices.push_back(item.layout_block_index);
        }
    }

    for (std::size_t index = 0; index < page.layout.blocks.size(); ++index) {
        const int block_index = static_cast<int>(index);
        if (seen.insert(block_index).second) {
            indices.push_back(block_index);
        }
    }

    return indices;
}

std::string normalizeFurnitureText(const std::string& text) {
    std::string normalized;
    bool previous_space = true;
    bool previous_digit = false;
    for (const unsigned char value : text) {
        if (std::isdigit(value)) {
            if (!previous_digit) {
                normalized += '#';
            }
            previous_digit = true;
            previous_space = false;
        } else if (std::isspace(value)) {
            if (!previous_space && !normalized.empty()) {
                normalized += ' ';
            }
            previous_space = true;
            previous_digit = false;
        } else {
            normalized += static_cast<char>(std::tolower(value));
            previous_space = false;
            previous_digit = false;
        }
    }
    if (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }
    return normalized;
}

std::string furnitureSignature(const document::PipelinePageArtifacts& page, const document::LayoutBlock& layout_block) {
    if (layout_block.type != document::LayoutBlockType::Header &&
        layout_block.type != document::LayoutBlockType::Footer) {
        return {};
    }
    const std::string text = normalizeFurnitureText(joinLayoutBlockText(page.text, layout_block));
    if (text.empty()) {
        return {};
    }
    return std::to_string(static_cast<int>(layout_block.type)) + ':' + text;
}

std::set<std::string> repeatedFurniture(const std::vector<document::PipelinePageArtifacts>& pages) {
    std::map<std::string, std::set<int>> pages_by_signature;
    for (const document::PipelinePageArtifacts& page : pages) {
        for (const document::LayoutBlock& block : page.layout.blocks) {
            const std::string signature = furnitureSignature(page, block);
            if (!signature.empty()) {
                pages_by_signature[signature].insert(page.page_index);
            }
        }
    }

    std::set<std::string> repeated;
    for (const auto& [signature, page_indices] : pages_by_signature) {
        if (page_indices.size() >= 2) {
            repeated.insert(signature);
        }
    }
    return repeated;
}

} // namespace

bool DocumentAssembler::assemble(const DocumentAssembleRequest& request,
                                 document::ParsedDocument& document,
                                 document::PipelineArtifacts& artifacts) const {
    if (!hasMatchingPageCounts(request)) {
        return false;
    }

    document = {};
    artifacts = {};
    document.source.path = request.source_path;
    document.source.type = request.source_type;
    document.dpi = request.dpi;
    artifacts.pages.reserve(request.pages.size());

    for (std::size_t index = 0; index < request.pages.size(); ++index) {
        artifacts.pages.push_back({
            request.pages[index].page_index,
            request.pages[index].page_number,
            request.pages[index],
            request.page_texts[index],
            request.page_layouts[index],
            request.page_reading_orders[index],
            request.page_tables[index],
        });
    }

    const std::set<std::string> repeated_furniture = repeatedFurniture(artifacts.pages);
    for (const document::PipelinePageArtifacts& parsed_page : artifacts.pages) {
        std::vector<int> included_indices;
        for (const int layout_block_index : orderedLayoutBlockIndices(parsed_page)) {
            const document::LayoutBlock& layout_block =
                parsed_page.layout.blocks[static_cast<std::size_t>(layout_block_index)];
            const std::string signature = furnitureSignature(parsed_page, layout_block);
            if (signature.empty() || repeated_furniture.find(signature) == repeated_furniture.end()) {
                included_indices.push_back(layout_block_index);
            }
        }

        std::map<std::string, std::string> related_block_ids;
        for (std::size_t offset = 0; offset < included_indices.size(); ++offset) {
            const document::LayoutBlock& layout_block =
                parsed_page.layout.blocks[static_cast<std::size_t>(included_indices[offset])];
            related_block_ids[layout_block.id] = blockId(parsed_page.page_number, document.blocks.size() + offset);
        }
        for (const int layout_block_index : included_indices) {
            const auto& layout_block = parsed_page.layout.blocks[static_cast<std::size_t>(layout_block_index)];
            document.blocks.push_back(
                makeDocumentBlock(parsed_page, layout_block, related_block_ids[layout_block.id], related_block_ids));
        }
    }

    return true;
}

} // namespace doc_parser::assembly
