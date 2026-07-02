#pragma once

#include "document/text_model.h"
#include "pdf/render_service.h"

#include <filesystem>
#include <string>
#include <vector>

namespace doc_parser::exporter {

struct JsonManifestInput {
    std::string source_path;
    int dpi = 200;
    bool debug = false;
    std::filesystem::path output_path;
    const std::vector<pdf::RenderedPage>* rendered_pages = nullptr;
    const std::vector<document::PageText>* page_texts = nullptr;
};

class JsonManifestWriter {
public:
    bool write(const JsonManifestInput& input) const;
};

} // namespace doc_parser::exporter
