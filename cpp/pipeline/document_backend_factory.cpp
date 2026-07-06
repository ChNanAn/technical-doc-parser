#include "pipeline/document_backend_factory.h"

#if DOC_PARSER_ENABLE_PDFIUM
#include "pipeline/pdfium_document_backend.h"
#endif

namespace doc_parser::pipeline {

std::unique_ptr<IDocumentBackend> createDocumentBackend(const std::string& backend_name) {
#if DOC_PARSER_ENABLE_PDFIUM
    if (backend_name == "auto" || backend_name == "pdfium") {
        return std::make_unique<PdfiumDocumentBackend>();
    }
#endif

    return nullptr;
}

std::unique_ptr<IDocumentBackend> createDefaultDocumentBackend() { return createDocumentBackend("auto"); }

} // namespace doc_parser::pipeline
