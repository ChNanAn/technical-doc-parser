#pragma once

#include "export/document_exporter.h"

namespace doc_parser::exporter {

class MarkdownDocumentExporter final : public IDocumentExporter {
public:
    bool write(const DocumentExportRequest& request) const override;
};

class HtmlDocumentExporter final : public IDocumentExporter {
public:
    bool write(const DocumentExportRequest& request) const override;
};

} // namespace doc_parser::exporter
