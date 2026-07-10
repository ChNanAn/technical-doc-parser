#include "document_source/document_source_factory.h"

#if DOC_PARSER_ENABLE_PDFIUM
#include "document_source/pdf/pdf_document_backend.h"
#endif

#include <memory>

namespace doc_parser::document_source {

DocumentSourceBundle createDocumentSource(const std::string& source_name) {
    (void)source_name;

#if DOC_PARSER_ENABLE_PDFIUM
    if (source_name == "auto" || source_name == "pdf") {
        auto source = std::make_unique<doc_parser::document_source::pdf::PdfDocumentBackend>();
        DocumentSourceBundle bundle;
        bundle.renderer = source.get();
        bundle.native_text_extractor = source.get();
        bundle.source = std::move(source);
        return bundle;
    }
#endif

    return {};
}

} // namespace doc_parser::document_source
