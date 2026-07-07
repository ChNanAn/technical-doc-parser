#include "pipeline/document_backend_factory.h"

#if DOC_PARSER_ENABLE_PDFIUM
#include "backend/pdf/pdf_document_backend.h"
#endif

#include <utility>

namespace doc_parser::pipeline {

DocumentBackendBundle createDocumentBackend(const std::string& backend_name) {
    (void)backend_name;

#if DOC_PARSER_ENABLE_PDFIUM
    if (backend_name == "auto" || backend_name == "pdf") {
        auto backend = std::make_unique<doc_parser::backend::pdf::PdfDocumentBackend>();
        DocumentBackendBundle bundle;
        bundle.renderer = backend.get();
        bundle.native_text_extractor = backend.get();
        bundle.source = std::move(backend);
        return bundle;
    }
#endif

    return {};
}

} // namespace doc_parser::pipeline
