#pragma once

#include "export/document_exporter.h"

namespace doc_parser::exporter {

class JsonDocumentExporter final : public IDocumentExporter {
public:
    common::Status write(const DocumentExportRequest& request) const override;
};

} // namespace doc_parser::exporter
