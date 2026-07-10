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

bool PdfDocumentBackend::renderPages(const pipeline::PipelineContext& context,
                                     std::vector<document::PageArtifact>& pages) const {
    const doc_parser::pdf::RenderService render;
    return render.renderPages(source_,
                              {
                                  context.render.dpi,
                                  context.output.root,
                                  context.output.pages_dir,
                              },
                              pages);
}

bool PdfDocumentBackend::extractNativeText(const pipeline::PipelineContext& context,
                                           std::vector<document::PageText>& page_texts) const {
    const doc_parser::pdf::TextService text;
    return text.extractText(source_, context.render.dpi, page_texts);
}

} // namespace doc_parser::document_source::pdf
