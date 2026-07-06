#include "assembly/document_assembler.h"

#include <algorithm>
#include <sstream>
#include <string>

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

document::DocumentBlock makeDocumentBlock(const document::ParsedPage& page,
                                          const document::LayoutBlock& layout_block,
                                          std::size_t block_index) {
    document::DocumentBlock block;
    block.id = blockId(page.page_number, block_index);
    block.type = toDocumentBlockType(layout_block.type);
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
            block.table_rows = table->rows;
            block.text = tableText(*table);
            block.confidence = std::min(block.confidence, table->confidence);
        }
    }

    return block;
}

bool hasMatchingPageCounts(const DocumentAssembleRequest& request) {
    return request.pages.size() == request.page_texts.size() && request.pages.size() == request.page_layouts.size() &&
           request.pages.size() == request.page_tables.size();
}

} // namespace

bool DocumentAssembler::assemble(const DocumentAssembleRequest& request, document::ParsedDocument& document) const {
    if (!hasMatchingPageCounts(request)) {
        return false;
    }

    document = {};
    document.source.path = request.source_path;
    document.source.type = request.source_type;
    document.dpi = request.dpi;
    document.pages.reserve(request.pages.size());

    for (std::size_t index = 0; index < request.pages.size(); ++index) {
        document.pages.push_back({
            request.pages[index].page_index,
            request.pages[index].page_number,
            request.pages[index],
            request.page_texts[index],
            request.page_layouts[index],
            request.page_tables[index],
        });

        const document::ParsedPage& parsed_page = document.pages.back();
        for (const auto& layout_block : parsed_page.layout.blocks) {
            document.blocks.push_back(makeDocumentBlock(parsed_page, layout_block, document.blocks.size()));
        }
    }

    return true;
}

} // namespace doc_parser::assembly
