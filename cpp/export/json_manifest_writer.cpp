#include "export/json_manifest_writer.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

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

}  // namespace

bool JsonManifestWriter::write(const JsonManifestInput& input) const {
    if (input.rendered_pages == nullptr || input.output_path.empty()) {
        return false;
    }
    if (input.debug &&
        (input.page_texts == nullptr || input.page_texts->size() != input.rendered_pages->size())) {
        std::cerr << "error: debug manifest requires one PageText per rendered page\n";
        return false;
    }

    nlohmann::json manifest;
    manifest["source"] = {
        {"path", input.source_path},
        {"type", "pdf"},
    };
    manifest["render"] = {
        {"dpi", input.dpi},
    };
    manifest["pages"] = nlohmann::json::array();

    for (std::size_t index = 0; index < input.rendered_pages->size(); ++index) {
        const auto& page = input.rendered_pages->at(index);
        nlohmann::json page_json = {
            {"page_index", page.page_index},
            {"page_number", page.page_number},
            {"image", page.relative_image},
        };
        if (input.debug) {
            page_json["debug"]["text"] = pageTextToJson(input.page_texts->at(index));
        }
        manifest["pages"].push_back(page_json);
    }

    std::ofstream manifest_file(input.output_path);
    if (!manifest_file) {
        std::cerr << "error: failed to write manifest: " << input.output_path << '\n';
        return false;
    }

    manifest_file << manifest.dump(2) << '\n';
    return true;
}

}  // namespace doc_parser::exporter
