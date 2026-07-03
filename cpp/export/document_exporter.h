#pragma once

#include "document/parsed_document.h"

#include <filesystem>
#include <memory>

namespace doc_parser::exporter {

struct DocumentExportRequest {
    bool debug = false;
    std::filesystem::path output_path;
    const document::ParsedDocument* document = nullptr;
};

class IDocumentExporter {
public:
    virtual ~IDocumentExporter() = default;

    virtual bool write(const DocumentExportRequest& request) const = 0;
};

std::unique_ptr<IDocumentExporter> createDefaultDocumentExporter();

} // namespace doc_parser::exporter
