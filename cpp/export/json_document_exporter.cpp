#include "export/json_document_exporter.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>

namespace doc_parser::exporter {
namespace {

const char* textSourceToString(document::TextSource source) {
    switch (source) {
    case document::TextSource::PdfTextLayer:
        return "pdf_text_layer";
    case document::TextSource::Ocr:
        return "ocr";
    case document::TextSource::Mixed:
        return "mixed";
    case document::TextSource::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* layoutBlockTypeToString(document::LayoutBlockType type) {
    switch (type) {
    case document::LayoutBlockType::Title:
        return "title";
    case document::LayoutBlockType::Text:
        return "text";
    case document::LayoutBlockType::List:
        return "list";
    case document::LayoutBlockType::Table:
        return "table";
    case document::LayoutBlockType::Figure:
        return "figure";
    case document::LayoutBlockType::Header:
        return "header";
    case document::LayoutBlockType::Footer:
        return "footer";
    case document::LayoutBlockType::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* documentBlockTypeToString(document::DocumentBlockType type) {
    switch (type) {
    case document::DocumentBlockType::Title:
        return "title";
    case document::DocumentBlockType::Paragraph:
        return "paragraph";
    case document::DocumentBlockType::List:
        return "list";
    case document::DocumentBlockType::Table:
        return "table";
    case document::DocumentBlockType::Figure:
        return "figure";
    case document::DocumentBlockType::Header:
        return "header";
    case document::DocumentBlockType::Footer:
        return "footer";
    case document::DocumentBlockType::Unknown:
        return "unknown";
    }
    return "unknown";
}

nlohmann::json bboxToJson(const document::BBox& bbox) {
    return {
        {"x0", bbox.x0},
        {"y0", bbox.y0},
        {"x1", bbox.x1},
        {"y1", bbox.y1},
    };
}

nlohmann::json tableRowsToJson(const std::vector<document::TableRow>& table_rows) {
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& row : table_rows) {
        nlohmann::json cells = nlohmann::json::array();
        for (const auto& cell : row.cells) {
            cells.push_back({
                {"row_index", cell.row_index},
                {"column_index", cell.column_index},
                {"row_span", cell.row_span},
                {"column_span", cell.column_span},
                {"is_header", cell.is_header},
                {"text", cell.text},
                {"bbox", bboxToJson(cell.bbox)},
                {"confidence", cell.confidence},
            });
        }

        rows.push_back({
            {"row_index", row.row_index},
            {"bbox", bboxToJson(row.bbox)},
            {"confidence", row.confidence},
            {"is_header", row.is_header},
            {"cells", cells},
        });
    }
    return rows;
}

nlohmann::json documentBlocksToJson(const std::vector<document::DocumentBlock>& document_blocks) {
    nlohmann::json blocks = nlohmann::json::array();
    for (const auto& block : document_blocks) {
        nlohmann::json block_json = {
            {"id", block.id},
            {"type", documentBlockTypeToString(block.type)},
            {"source_label", block.source_label},
            {"related_block_id", block.related_block_id},
            {"page_index", block.page_index},
            {"page_number", block.page_number},
            {"bbox", bboxToJson(block.bbox)},
            {"confidence", block.confidence},
            {"text", block.text},
        };
        if (!block.table_id.empty()) {
            block_json["table_id"] = block.table_id;
            block_json["table_continuation_group_id"] = block.table_continuation_group_id;
            block_json["table_continues_from_previous_page"] = block.table_continues_from_previous_page;
            block_json["table_continues_on_next_page"] = block.table_continues_on_next_page;
            block_json["rows"] = tableRowsToJson(block.table_rows);
        }
        blocks.push_back(block_json);
    }
    return blocks;
}

nlohmann::json pageTextToJson(const document::PageText& page_text) {
    nlohmann::json lines = nlohmann::json::array();
    for (const auto& line : page_text.lines) {
        nlohmann::json spans = nlohmann::json::array();
        for (const auto& span : line.spans) {
            spans.push_back({
                {"text", span.text},
                {"bbox", bboxToJson(span.bbox)},
                {"source", textSourceToString(span.source)},
                {"confidence", span.confidence},
            });
        }

        lines.push_back({
            {"text", line.text},
            {"bbox", bboxToJson(line.bbox)},
            {"source", textSourceToString(line.source)},
            {"confidence", line.confidence},
            {"spans", spans},
        });
    }

    return {
        {"has_text", page_text.has_text},
        {"preferred_source", textSourceToString(page_text.preferred_source)},
        {"lines", lines},
    };
}

nlohmann::json pageLayoutToJson(const document::PageLayout& page_layout) {
    nlohmann::json blocks = nlohmann::json::array();
    for (const auto& block : page_layout.blocks) {
        blocks.push_back({
            {"id", block.id},
            {"type", layoutBlockTypeToString(block.type)},
            {"source_label", block.source_label},
            {"related_block_id", block.related_block_id},
            {"bbox", bboxToJson(block.bbox)},
            {"confidence", block.confidence},
            {"reading_order_hint", block.reading_order_hint},
            {"text_line_indices", block.text_line_indices},
        });
    }

    return {
        {"blocks", blocks},
    };
}

nlohmann::json pageReadingOrderToJson(const document::PageReadingOrder& page_reading_order) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& item : page_reading_order.items) {
        items.push_back({
            {"layout_block_id", item.layout_block_id},
            {"layout_block_index", item.layout_block_index},
            {"sequence_index", item.sequence_index},
        });
    }

    return {
        {"items", items},
    };
}

nlohmann::json pageTablesToJson(const document::PageTables& page_tables) {
    nlohmann::json tables = nlohmann::json::array();
    for (const auto& table : page_tables.tables) {
        nlohmann::json columns = nlohmann::json::array();
        for (const auto& column : table.columns) {
            columns.push_back({
                {"column_index", column.column_index},
                {"bbox", bboxToJson(column.bbox)},
                {"confidence", column.confidence},
            });
        }
        nlohmann::json structure_objects = nlohmann::json::array();
        for (const auto& object : table.structure_objects) {
            structure_objects.push_back({
                {"label", object.label},
                {"bbox", bboxToJson(object.bbox)},
                {"confidence", object.confidence},
            });
        }
        tables.push_back({
            {"id", table.id},
            {"layout_block_id", table.layout_block_id},
            {"bbox", bboxToJson(table.bbox)},
            {"confidence", table.confidence},
            {"source_label", table.source_label},
            {"continuation_group_id", table.continuation_group_id},
            {"continues_from_previous_page", table.continues_from_previous_page},
            {"continues_on_next_page", table.continues_on_next_page},
            {"columns", columns},
            {"rows", tableRowsToJson(table.rows)},
            {"structure_objects", structure_objects},
        });
    }

    return {
        {"tables", tables},
    };
}

nlohmann::json debugImagesToJson(const std::vector<document::DebugImageArtifact>& images) {
    nlohmann::json image_json = nlohmann::json::array();
    for (const auto& image : images) {
        image_json.push_back({
            {"name", image.name},
            {"image", image.relative_image},
        });
    }
    return image_json;
}

} // namespace

bool JsonDocumentExporter::write(const DocumentExportRequest& request) const {
    if (request.document == nullptr || request.output_path.empty()) {
        return false;
    }

    const document::ParsedDocument& document = *request.document;

    nlohmann::json manifest;
    manifest["source"] = {
        {"path", document.source.path},
        {"type", document.source.type},
    };
    manifest["render"] = {
        {"dpi", document.dpi},
    };
    manifest["blocks"] = documentBlocksToJson(document.blocks);
    manifest["pages"] = nlohmann::json::array();

    const document::PipelineArtifacts* artifacts = request.artifacts;
    if (artifacts != nullptr) {
        for (const auto& page : artifacts->pages) {
            nlohmann::json page_json = {
                {"page_index", page.page_index},
                {"page_number", page.page_number},
                {"image", page.image.relative_image},
            };
            if (request.debug) {
                page_json["debug"]["text"] = pageTextToJson(page.text);
                page_json["debug"]["layout"] = pageLayoutToJson(page.layout);
                page_json["debug"]["reading_order"] = pageReadingOrderToJson(page.reading_order);
                page_json["debug"]["tables"] = pageTablesToJson(page.tables);
                if (!page.image.debug_images.empty()) {
                    page_json["debug"]["images"] = debugImagesToJson(page.image.debug_images);
                }
            }
            manifest["pages"].push_back(page_json);
        }
    }

    std::ofstream manifest_file(request.output_path);
    if (!manifest_file) {
        std::cerr << "error: failed to write manifest: " << request.output_path << '\n';
        return false;
    }

    manifest_file << manifest.dump(2) << '\n';
    return true;
}

} // namespace doc_parser::exporter
