#include "export/json_document_exporter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace doc_parser::exporter {
namespace {

constexpr const char* kDocumentSchema = "https://github.com/ChNanAn/technical-doc-parser/schemas/"
                                        "document.v1.schema.json";
constexpr const char* kDebugExtension = "io.github.chnanan.technical-doc-parser.pipeline_debug";

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

const char* documentStatusToString(document::DocumentStatus status) {
    return status == document::DocumentStatus::Partial ? "partial" : "complete";
}

bool hasPositiveArea(const document::BBox& bbox) {
    return std::isfinite(bbox.x0) && std::isfinite(bbox.y0) && std::isfinite(bbox.x1) && std::isfinite(bbox.y1) &&
           bbox.x0 >= 0.0 && bbox.y0 >= 0.0 && bbox.x0 < bbox.x1 && bbox.y0 < bbox.y1;
}

bool isScore(double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; }

nlohmann::json bboxToJson(const document::BBox& bbox) {
    return nlohmann::json::array({bbox.x0, bbox.y0, bbox.x1, bbox.y1});
}

nlohmann::json debugBboxToJson(const document::BBox& bbox) {
    return {
        {"x0", bbox.x0},
        {"y0", bbox.y0},
        {"x1", bbox.x1},
        {"y1", bbox.y1},
    };
}

nlohmann::json scoreToJson(double value, const char* kind) {
    return {
        {"value", value},
        {"kind", kind},
    };
}

std::string basename(const std::string& path) {
    const std::size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

std::string mediaTypeFromSourceType(const std::string& source_type) {
    if (source_type.find('/') != std::string::npos) {
        return source_type;
    }
    if (source_type == "pdf") {
        return "application/pdf";
    }
    if (source_type == "png") {
        return "image/png";
    }
    if (source_type == "jpg" || source_type == "jpeg") {
        return "image/jpeg";
    }
    if (source_type == "tif" || source_type == "tiff") {
        return "image/tiff";
    }
    return "application/octet-stream";
}

bool isAbsoluteLocalPath(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (path.front() == '/' || path.front() == '\\') {
        return true;
    }
    return path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
           (path[2] == '/' || path[2] == '\\');
}

std::string documentId(const document::ParsedDocument& document) {
    if (!document.document_id.empty()) {
        return document.document_id;
    }
    const std::string filename = basename(document.source.path);
    return filename.empty() ? "document" : "document:" + filename;
}

struct PageLookup {
    std::map<int, std::string> ids_by_number;
    std::set<std::string> ids;
    std::map<std::string, std::pair<double, double>> dimensions_by_id;
};

PageLookup pageLookup(const std::vector<document::DocumentPage>& pages) {
    PageLookup lookup;
    for (const document::DocumentPage& page : pages) {
        lookup.ids_by_number.emplace(page.number, page.id);
        lookup.ids.insert(page.id);
        lookup.dimensions_by_id.emplace(page.id, std::make_pair(page.width, page.height));
    }
    return lookup;
}

bool bboxFitsPage(const document::BBox& bbox, const std::string& page_id, const PageLookup& pages) {
    const auto found = pages.dimensions_by_id.find(page_id);
    return found != pages.dimensions_by_id.end() && hasPositiveArea(bbox) && bbox.x1 <= found->second.first &&
           bbox.y1 <= found->second.second;
}

std::string resolvedPageId(const std::string& page_id, int page_number, const PageLookup& pages) {
    if (!page_id.empty() && pages.ids.find(page_id) != pages.ids.end()) {
        return page_id;
    }
    const auto found = pages.ids_by_number.find(page_number);
    return found == pages.ids_by_number.end() ? std::string{} : found->second;
}

nlohmann::json sourceRefsToJson(const std::vector<document::SourceReference>& source_refs, const PageLookup& pages) {
    nlohmann::json result = nlohmann::json::array();
    for (const document::SourceReference& source_ref : source_refs) {
        if (pages.ids.find(source_ref.page_id) == pages.ids.end()) {
            continue;
        }
        nlohmann::json value = {
            {"page_id", source_ref.page_id},
            {"kind", textSourceToString(source_ref.source)},
        };
        if (bboxFitsPage(source_ref.bbox, source_ref.page_id, pages)) {
            value["bbox"] = bboxToJson(source_ref.bbox);
        }
        if (!source_ref.text.empty()) {
            value["text"] = source_ref.text;
        }
        result.push_back(std::move(value));
    }
    return result;
}

nlohmann::json tableRowsToJson(const std::vector<document::TableRow>& table_rows,
                               const std::string& page_id,
                               const PageLookup& pages) {
    nlohmann::json rows = nlohmann::json::array();
    for (const document::TableRow& row : table_rows) {
        nlohmann::json row_json = {
            {"row_index", row.row_index},
            {"is_header", row.is_header},
            {"cells", nlohmann::json::array()},
        };
        if (bboxFitsPage(row.bbox, page_id, pages)) {
            row_json["bbox"] = bboxToJson(row.bbox);
        }

        for (const document::TableCell& cell : row.cells) {
            nlohmann::json cell_json = {
                {"row_index", cell.row_index},
                {"column_index", cell.column_index},
                {"row_span", cell.row_span},
                {"column_span", cell.column_span},
                {"is_header", cell.is_header},
                {"text", cell.text},
            };
            if (bboxFitsPage(cell.bbox, page_id, pages)) {
                cell_json["bbox"] = bboxToJson(cell.bbox);
            }
            if (isScore(cell.confidence)) {
                cell_json["score"] = scoreToJson(cell.confidence, "model_score");
            }
            nlohmann::json source_refs = sourceRefsToJson(cell.source_refs, pages);
            if (!source_refs.empty()) {
                cell_json["source_refs"] = std::move(source_refs);
            }
            row_json["cells"].push_back(std::move(cell_json));
        }
        rows.push_back(std::move(row_json));
    }
    return rows;
}

nlohmann::json documentBlocksToJson(const std::vector<document::DocumentBlock>& document_blocks,
                                    const PageLookup& pages) {
    nlohmann::json blocks = nlohmann::json::array();
    for (const document::DocumentBlock& block : document_blocks) {
        nlohmann::json block_json = {
            {"id", block.id},
            {"type", documentBlockTypeToString(block.type)},
            {"text", block.text},
        };

        const std::string page_id = resolvedPageId(block.page_id, block.page_number, pages);
        if (!page_id.empty()) {
            block_json["page_id"] = page_id;
            if (bboxFitsPage(block.bbox, page_id, pages)) {
                block_json["bbox"] = bboxToJson(block.bbox);
            }
        }
        if (isScore(block.confidence)) {
            block_json["score"] = scoreToJson(block.confidence, "model_score");
        }
        nlohmann::json source_refs = sourceRefsToJson(block.source_refs, pages);
        if (!source_refs.empty()) {
            block_json["source_refs"] = std::move(source_refs);
        }
        if (!block.source_label.empty()) {
            block_json["metadata"] = {{"source_label", block.source_label}};
        }
        if (!block.table_id.empty() || !block.table_rows.empty()) {
            nlohmann::json table = {
                {"rows", tableRowsToJson(block.table_rows, page_id, pages)},
                {"continues_from_previous_page", block.table_continues_from_previous_page},
                {"continues_on_next_page", block.table_continues_on_next_page},
            };
            if (!block.table_id.empty()) {
                table["id"] = block.table_id;
            }
            if (!block.table_continuation_group_id.empty()) {
                table["continuation_group_id"] = block.table_continuation_group_id;
            }
            block_json["table"] = std::move(table);
        }
        blocks.push_back(std::move(block_json));
    }
    return blocks;
}

nlohmann::json pagesToJson(const std::vector<document::DocumentPage>& pages) {
    nlohmann::json result = nlohmann::json::array();
    for (const document::DocumentPage& page : pages) {
        nlohmann::json value = {
            {"id", page.id},
            {"number", page.number},
            {"width", page.width},
            {"height", page.height},
        };
        if (!page.image_uri.empty() && !isAbsoluteLocalPath(page.image_uri)) {
            const std::string media_type = page.image_media_type.find('/') == std::string::npos ? "application/"
                                                                                                  "octet-stream"
                                                                                                : page.image_media_type;
            value["image"] = {
                {"id", page.image_id.empty() ? page.id + "_image" : page.image_id},
                {"uri", page.image_uri},
                {"media_type", media_type},
            };
        }
        result.push_back(std::move(value));
    }
    return result;
}

nlohmann::json relationsToJson(const std::vector<document::DocumentRelation>& relations) {
    nlohmann::json result = nlohmann::json::array();
    for (const document::DocumentRelation& relation : relations) {
        nlohmann::json value = {
            {"type", relation.type},
            {"from_block_id", relation.from_block_id},
            {"to_block_id", relation.to_block_id},
        };
        if (!relation.id.empty()) {
            value["id"] = relation.id;
        }
        result.push_back(std::move(value));
    }
    return result;
}

nlohmann::json warningsToJson(const std::vector<document::DocumentWarning>& warnings) {
    nlohmann::json result = nlohmann::json::array();
    for (const document::DocumentWarning& warning : warnings) {
        nlohmann::json value = {
            {"code", warning.code},
            {"message", warning.message},
        };
        if (!warning.stage.empty()) {
            value["stage"] = warning.stage;
        }
        if (!warning.page_id.empty()) {
            value["page_id"] = warning.page_id;
        }
        if (!warning.block_id.empty()) {
            value["block_id"] = warning.block_id;
        }
        if (warning.occurrence_count > 1U) {
            value["occurrence_count"] = warning.occurrence_count;
        }
        if (!warning.page_ids.empty()) {
            value["page_ids"] = warning.page_ids;
        }
        if (!warning.details.empty()) {
            value["details"] = warning.details;
        }
        result.push_back(std::move(value));
    }
    return result;
}

nlohmann::json pageTextToJson(const document::PageText& page_text) {
    nlohmann::json lines = nlohmann::json::array();
    for (const document::TextLine& line : page_text.lines) {
        nlohmann::json spans = nlohmann::json::array();
        for (const document::TextSpan& span : line.spans) {
            spans.push_back({
                {"text", span.text},
                {"bbox", debugBboxToJson(span.bbox)},
                {"source", textSourceToString(span.source)},
                {"confidence", span.confidence},
            });
        }
        lines.push_back({
            {"text", line.text},
            {"bbox", debugBboxToJson(line.bbox)},
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
    for (const document::LayoutBlock& block : page_layout.blocks) {
        blocks.push_back({
            {"id", block.id},
            {"type", layoutBlockTypeToString(block.type)},
            {"source_label", block.source_label},
            {"related_block_id", block.related_block_id},
            {"bbox", debugBboxToJson(block.bbox)},
            {"confidence", block.confidence},
            {"reading_order_hint", block.reading_order_hint},
            {"text_line_indices", block.text_line_indices},
        });
    }
    return {{"blocks", blocks}};
}

nlohmann::json pageReadingOrderToJson(const document::PageReadingOrder& page_reading_order) {
    nlohmann::json items = nlohmann::json::array();
    for (const document::ReadingOrderItem& item : page_reading_order.items) {
        items.push_back({
            {"layout_block_id", item.layout_block_id},
            {"layout_block_index", item.layout_block_index},
            {"sequence_index", item.sequence_index},
        });
    }

    nlohmann::json placements = nlohmann::json::array();
    for (const document::ReadingOrderPlacement& placement : page_reading_order.trace.placements) {
        placements.push_back({
            {"layout_block_id", placement.layout_block_id},
            {"group", placement.group},
            {"band_index", placement.band_index},
            {"column_start", placement.column_start},
            {"column_end", placement.column_end},
        });
    }

    nlohmann::json cycle_breaks = nlohmann::json::array();
    for (const document::ReadingOrderCycleBreak& cycle_break : page_reading_order.trace.cycle_breaks) {
        cycle_breaks.push_back({
            {"from_layout_block_id", cycle_break.from_layout_block_id},
            {"to_layout_block_id", cycle_break.to_layout_block_id},
            {"reason", cycle_break.reason},
            {"confidence", cycle_break.confidence},
        });
    }

    return {
        {"items", items},
        {"trace",
         {
             {"algorithm", page_reading_order.trace.algorithm},
             {"placements", placements},
             {"edge_counts", page_reading_order.trace.edge_counts},
             {"cycle_breaks", cycle_breaks},
         }},
    };
}

nlohmann::json debugTableRowsToJson(const std::vector<document::TableRow>& table_rows) {
    nlohmann::json rows = nlohmann::json::array();
    for (const document::TableRow& row : table_rows) {
        nlohmann::json cells = nlohmann::json::array();
        for (const document::TableCell& cell : row.cells) {
            cells.push_back({
                {"row_index", cell.row_index},
                {"column_index", cell.column_index},
                {"row_span", cell.row_span},
                {"column_span", cell.column_span},
                {"is_header", cell.is_header},
                {"text", cell.text},
                {"bbox", debugBboxToJson(cell.bbox)},
                {"confidence", cell.confidence},
            });
        }
        rows.push_back({
            {"row_index", row.row_index},
            {"bbox", debugBboxToJson(row.bbox)},
            {"confidence", row.confidence},
            {"is_header", row.is_header},
            {"cells", std::move(cells)},
        });
    }
    return rows;
}

nlohmann::json pageTablesToJson(const document::PageTables& page_tables) {
    nlohmann::json tables = nlohmann::json::array();
    for (const document::Table& table : page_tables.tables) {
        nlohmann::json columns = nlohmann::json::array();
        for (const document::TableColumn& column : table.columns) {
            columns.push_back({
                {"column_index", column.column_index},
                {"bbox", debugBboxToJson(column.bbox)},
                {"confidence", column.confidence},
            });
        }
        nlohmann::json structure_objects = nlohmann::json::array();
        for (const document::TableStructureObject& object : table.structure_objects) {
            structure_objects.push_back({
                {"label", object.label},
                {"bbox", debugBboxToJson(object.bbox)},
                {"confidence", object.confidence},
            });
        }
        tables.push_back({
            {"id", table.id},
            {"layout_block_id", table.layout_block_id},
            {"bbox", debugBboxToJson(table.bbox)},
            {"confidence", table.confidence},
            {"source_label", table.source_label},
            {"continuation_group_id", table.continuation_group_id},
            {"continues_from_previous_page", table.continues_from_previous_page},
            {"continues_on_next_page", table.continues_on_next_page},
            {"columns", columns},
            {"rows", debugTableRowsToJson(table.rows)},
            {"structure_objects", structure_objects},
        });
    }
    return {{"tables", tables}};
}

nlohmann::json debugImagesToJson(const std::vector<document::DebugImageArtifact>& images) {
    nlohmann::json result = nlohmann::json::array();
    for (const document::DebugImageArtifact& image : images) {
        result.push_back({
            {"name", image.name},
            {"image", image.relative_image},
        });
    }
    return result;
}

void addDebugExtensions(nlohmann::json& pages, const document::PipelineArtifacts& artifacts) {
    std::map<int, const document::PipelinePageArtifacts*> artifacts_by_number;
    for (const document::PipelinePageArtifacts& page : artifacts.pages) {
        artifacts_by_number[page.page_number] = &page;
    }

    for (nlohmann::json& page : pages) {
        const auto found = artifacts_by_number.find(page.at("number").get<int>());
        if (found == artifacts_by_number.end()) {
            continue;
        }
        const document::PipelinePageArtifacts& artifact = *found->second;
        nlohmann::json debug = {
            {"text", pageTextToJson(artifact.text)},
            {"layout", pageLayoutToJson(artifact.layout)},
            {"reading_order", pageReadingOrderToJson(artifact.reading_order)},
            {"tables", pageTablesToJson(artifact.tables)},
        };
        if (!artifact.image.debug_images.empty()) {
            debug["images"] = debugImagesToJson(artifact.image.debug_images);
        }
        page["extensions"][kDebugExtension] = std::move(debug);
    }
}

common::Status invalidCoreModel(std::string code, std::string message) {
    return common::Status::error(std::move(code), std::move(message), "export");
}

common::Status validateCoreModel(const document::ParsedDocument& document) {
    if (document.status == document::DocumentStatus::Partial && document.warnings.empty()) {
        return invalidCoreModel("export.document.partial_unexplained",
                                "partial document has no warning explaining the degraded result");
    }
    if (!document.source.sha256.empty()) {
        const bool valid_sha256 =
            document.source.sha256.size() == 64U &&
            std::all_of(document.source.sha256.begin(), document.source.sha256.end(), [](unsigned char value) {
                return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
            });
        if (!valid_sha256) {
            return invalidCoreModel("export.document.invalid_source", "source sha256 is not lowercase hexadecimal");
        }
    }

    std::set<std::string> page_ids;
    std::set<int> page_numbers;
    for (std::size_t index = 0; index < document.pages.size(); ++index) {
        const document::DocumentPage& page = document.pages[index];
        const std::string location = "page at index " + std::to_string(index);
        if (page.id.empty()) {
            return invalidCoreModel("export.document.invalid_page", location + " has an empty id");
        }
        if (page.number <= 0) {
            return invalidCoreModel("export.document.invalid_page",
                                    location + " ('" + page.id + "') has a non-positive page number");
        }
        if (!std::isfinite(page.width) || !std::isfinite(page.height) || page.width <= 0.0 || page.height <= 0.0) {
            return invalidCoreModel("export.document.invalid_page",
                                    location + " ('" + page.id + "') has invalid dimensions");
        }
        if (!page_ids.insert(page.id).second) {
            return invalidCoreModel("export.document.invalid_page", "duplicate page id '" + page.id + "'");
        }
        if (!page_numbers.insert(page.number).second) {
            return invalidCoreModel("export.document.invalid_page",
                                    "duplicate page number " + std::to_string(page.number));
        }
    }

    std::set<std::string> block_ids;
    for (std::size_t index = 0; index < document.blocks.size(); ++index) {
        const document::DocumentBlock& block = document.blocks[index];
        if (block.id.empty()) {
            return invalidCoreModel("export.document.invalid_block",
                                    "block at index " + std::to_string(index) + " has an empty id");
        }
        if (!block_ids.insert(block.id).second) {
            return invalidCoreModel("export.document.invalid_block", "duplicate block id '" + block.id + "'");
        }
    }

    for (std::size_t index = 0; index < document.relations.size(); ++index) {
        const document::DocumentRelation& relation = document.relations[index];
        const std::string location = relation.id.empty() ? "relation at index " + std::to_string(index)
                                                         : "relation '" + relation.id + "'";
        if (relation.type.empty()) {
            return invalidCoreModel("export.document.invalid_relation", location + " has an empty type");
        }
        if (block_ids.find(relation.from_block_id) == block_ids.end()) {
            return invalidCoreModel("export.document.invalid_relation",
                                    location + " references unknown from_block_id '" + relation.from_block_id + "'");
        }
        if (block_ids.find(relation.to_block_id) == block_ids.end()) {
            return invalidCoreModel("export.document.invalid_relation",
                                    location + " references unknown to_block_id '" + relation.to_block_id + "'");
        }
        if (relation.from_block_id == relation.to_block_id) {
            return invalidCoreModel("export.document.invalid_relation",
                                    location + " is self-referential for block '" + relation.from_block_id + "'");
        }
    }
    for (std::size_t index = 0; index < document.warnings.size(); ++index) {
        const document::DocumentWarning& warning = document.warnings[index];
        const std::string location = "warning at index " + std::to_string(index);
        if (warning.code.empty()) {
            return invalidCoreModel("export.document.invalid_warning", location + " has an empty code");
        }
        if (warning.message.empty()) {
            return invalidCoreModel("export.document.invalid_warning",
                                    location + " ('" + warning.code + "') has an empty message");
        }
        if (!warning.page_id.empty() && page_ids.find(warning.page_id) == page_ids.end()) {
            return invalidCoreModel(
                "export.document.invalid_warning",
                location + " ('" + warning.code + "') references unknown page_id '" + warning.page_id + "'");
        }
        if (!warning.block_id.empty() && block_ids.find(warning.block_id) == block_ids.end()) {
            return invalidCoreModel(
                "export.document.invalid_warning",
                location + " ('" + warning.code + "') references unknown block_id '" + warning.block_id + "'");
        }
        if (warning.occurrence_count < 1U) {
            return invalidCoreModel("export.document.invalid_warning",
                                    location + " ('" + warning.code + "') has a non-positive occurrence_count");
        }
        if (!warning.page_id.empty() && !warning.page_ids.empty()) {
            return invalidCoreModel("export.document.invalid_warning",
                                    location + " ('" + warning.code + "') has both page_id and page_ids");
        }
        std::set<std::string> warning_page_ids;
        for (const std::string& page_id : warning.page_ids) {
            if (page_ids.find(page_id) == page_ids.end()) {
                return invalidCoreModel(
                    "export.document.invalid_warning",
                    location + " ('" + warning.code + "') references unknown page_id '" + page_id + "'");
            }
            if (!warning_page_ids.insert(page_id).second) {
                return invalidCoreModel(
                    "export.document.invalid_warning",
                    location + " ('" + warning.code + "') contains duplicate page_id '" + page_id + "'");
            }
        }
        if (warning.occurrence_count < warning_page_ids.size()) {
            return invalidCoreModel("export.document.invalid_warning",
                                    location + " ('" + warning.code + "') has fewer occurrences than affected pages");
        }
    }
    return common::Status::ok();
}

} // namespace

JsonDocumentSerializationResult JsonDocumentExporter::serialize(const JsonDocumentSerializationRequest& request) const {
    JsonDocumentSerializationResult result;
    if (request.document == nullptr) {
        result.status =
            common::Status::error("export.json.document_missing", "document export request has no document", "export");
        return result;
    }
    if (common::Status status = validateCoreModel(*request.document); !status.okStatus()) {
        result.status = std::move(status);
        return result;
    }

    try {
        const document::ParsedDocument& document = *request.document;
        const std::string filename =
            basename(document.source.filename.empty() ? document.source.path : document.source.filename);
        const std::string media_type = document.source.media_type.find('/') == std::string::npos
                                           ? mediaTypeFromSourceType(document.source.type)
                                           : document.source.media_type;
        nlohmann::json source = {{"media_type", media_type}};
        if (!filename.empty()) {
            source["filename"] = filename;
        }
        if (document.source.size_bytes.has_value()) {
            source["size_bytes"] = *document.source.size_bytes;
        }
        if (!document.source.sha256.empty()) {
            source["sha256"] = document.source.sha256;
        }

        nlohmann::json producer = {
            {"name", document.producer.name.empty() ? "technical-doc-parser" : document.producer.name},
            {"version", document.producer.version.empty() ? "unknown" : document.producer.version},
        };
        if (document.producer.git_revision.size() >= 7) {
            producer["git_revision"] = document.producer.git_revision;
        }
        if (!document.producer.run_id.empty()) {
            producer["run_id"] = document.producer.run_id;
        }

        nlohmann::json coordinate_space = {
            {"unit", "pixel"},
            {"origin", "top_left"},
            {"bbox_format", "xyxy"},
        };
        if (document.dpi > 0) {
            coordinate_space["dpi"] = document.dpi;
        }

        nlohmann::json pages = pagesToJson(document.pages);
        if (request.debug && request.artifacts != nullptr) {
            addDebugExtensions(pages, *request.artifacts);
        }

        const PageLookup page_lookup = pageLookup(document.pages);
        nlohmann::json manifest = {
            {"$schema", kDocumentSchema},
            {"schema_version", 1},
            {"document_id", documentId(document)},
            {"status", documentStatusToString(document.status)},
            {"source", std::move(source)},
            {"producer", std::move(producer)},
            {"coordinate_space", std::move(coordinate_space)},
            {"pages", std::move(pages)},
            {"blocks", documentBlocksToJson(document.blocks, page_lookup)},
            {"warnings", warningsToJson(document.warnings)},
        };
        if (!document.relations.empty()) {
            manifest["relations"] = relationsToJson(document.relations);
        }
        result.json = manifest.dump(2);
    } catch (const nlohmann::json::exception& error) {
        result.status = common::Status::error("export.json.serialization_failed",
                                              "failed to serialize JSON document: " + std::string(error.what()),
                                              "export");
        return result;
    }
    result.status = common::Status::ok();
    return result;
}

common::Status JsonDocumentExporter::write(const DocumentExportRequest& request) const {
    if (request.document == nullptr) {
        return common::Status::error(
            "export.json.document_missing", "document export request has no document", "export");
    }
    if (request.output_path.empty()) {
        return common::Status::error(
            "export.json.output_path_missing", "document export request has no output path", "export");
    }
    JsonDocumentSerializationResult serialization = serialize({
        request.debug,
        request.document,
        request.artifacts,
    });
    if (!serialization.ok()) {
        return serialization.status;
    }
    std::ofstream manifest_file(request.output_path);
    if (!manifest_file) {
        return common::Status::error(
            "export.json.open_failed", "failed to open JSON output: " + request.output_path.string(), "export", true);
    }

    manifest_file << serialization.json << '\n';
    manifest_file.flush();
    if (!manifest_file) {
        return common::Status::error(
            "export.json.write_failed", "failed to write JSON output: " + request.output_path.string(), "export", true);
    }
    return common::Status::ok();
}

} // namespace doc_parser::exporter
