#include "document_source/pdf/pdf_document_backend.h"

#include "document_source/pdf/render_service.h"
#include "document_source/pdf/text_service.h"

namespace doc_parser::document_source::pdf {

bool PdfDocumentBackend::open(const std::filesystem::path& input_path) {
    source_path_ = input_path.string();
    return source_.open(source_path_);
}

std::string PdfDocumentBackend::sourcePath() const { return source_path_; }

std::string PdfDocumentBackend::sourceType() const { return "pdf"; }

int PdfDocumentBackend::pageCount() const { return source_.pageCount(); }

bool PdfDocumentBackend::renderPages(const RenderRequest& request, std::vector<document::PageArtifact>& pages) const {
    const doc_parser::pdf::RenderService render;
    return render.renderPages(source_, request, pages);
}

bool PdfDocumentBackend::extractNativeText(const NativeTextRequest& request,
                                           std::vector<document::PageText>& page_texts) const {
    const doc_parser::pdf::TextService text;
    return text.extractText(source_, request.dpi, page_texts);
}

bool PdfDocumentBackend::renderPage(const RenderRequest& request, int page_index, document::PageArtifact& page) const {
    const doc_parser::pdf::RenderService render;
    return render.renderPage(source_, request, page_index, page);
}

bool PdfDocumentBackend::extractPageNativeText(const NativeTextRequest& request,
                                               int page_index,
                                               document::PageText& page_text) const {
    const doc_parser::pdf::TextService text;
    return text.extractPageText(source_, request.dpi, page_index, page_text);
}

} // namespace doc_parser::document_source::pdf
