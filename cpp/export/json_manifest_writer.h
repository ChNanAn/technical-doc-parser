#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"

#include <filesystem>
#include <string>
#include <vector>

namespace doc_parser::exporter {

struct JsonManifestInput {
    std::string source_path;
    int dpi = 200;
    bool debug = false;
    std::filesystem::path output_path;
    const std::vector<document::PageArtifact>* rendered_pages = nullptr;
    const std::vector<document::PageText>* page_texts = nullptr;
};

class JsonManifestWriter {
public:
    bool write(const JsonManifestInput& input) const;
};

} // namespace doc_parser::exporter
