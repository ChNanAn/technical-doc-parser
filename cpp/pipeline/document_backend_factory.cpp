#include "pipeline/document_backend_factory.h"

#if DOC_PARSER_ENABLE_PDFIUM
#include "pipeline/pdfium_document_backend.h"
#endif

namespace doc_parser::pipeline {

std::unique_ptr<IDocumentBackend> createDefaultDocumentBackend() {
#if DOC_PARSER_ENABLE_PDFIUM
    return std::make_unique<PdfiumDocumentBackend>();
#else
    return nullptr;
#endif
}

} // namespace doc_parser::pipeline
