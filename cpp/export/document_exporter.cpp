#include "export/document_exporter.h"

#include "export/json_document_exporter.h"

namespace doc_parser::exporter {

std::unique_ptr<IDocumentExporter> createDefaultDocumentExporter() { return std::make_unique<JsonDocumentExporter>(); }

} // namespace doc_parser::exporter
