#pragma once

#include "export/document_exporter.h"

#include <string>

namespace doc_parser::exporter {

struct JsonDocumentSerializationRequest {
    bool debug = false;
    const document::ParsedDocument* document = nullptr;
    const document::PipelineArtifacts* artifacts = nullptr;
};

struct JsonDocumentSerializationResult {
    common::Status status =
        common::Status::error("export.json.not_started", "JSON serialization has not started", "export");
    std::string json;

    bool ok() const { return status.okStatus(); }
};

class JsonDocumentExporter final : public IDocumentExporter {
public:
    JsonDocumentSerializationResult serialize(const JsonDocumentSerializationRequest& request) const;
    common::Status write(const DocumentExportRequest& request) const override;
};

} // namespace doc_parser::exporter
