#pragma once

#include "document/parsed_document.h"

#include <filesystem>

namespace doc_parser::exporter {

struct JsonManifestInput {
    bool debug = false;
    std::filesystem::path output_path;
    const document::ParsedDocument* document = nullptr;
};

class JsonManifestWriter {
public:
    bool write(const JsonManifestInput& input) const;
};

} // namespace doc_parser::exporter
