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
    case document::TextSource::Unknown:
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
    manifest["pages"] = nlohmann::json::array();

    for (const auto& page : document.pages) {
        nlohmann::json page_json = {
            {"page_index", page.page_index},
            {"page_number", page.page_number},
            {"image", page.image.relative_image},
        };
        if (request.debug) {
            page_json["debug"]["text"] = pageTextToJson(page.text);
            if (!page.image.debug_images.empty()) {
                page_json["debug"]["images"] = debugImagesToJson(page.image.debug_images);
            }
        }
        manifest["pages"].push_back(page_json);
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
