#include "pipeline/pdfium_document_backend.h"

#include "pdf/render_service.h"
#include "pdf/text_service.h"

namespace doc_parser::pipeline {

bool PdfiumDocumentBackend::open(const std::filesystem::path& input_path) {
    source_path_ = input_path.string();
    return source_.open(source_path_);
}

std::string PdfiumDocumentBackend::sourcePath() const { return source_path_; }

std::string PdfiumDocumentBackend::sourceType() const { return "pdf"; }

int PdfiumDocumentBackend::pageCount() const { return source_.pageCount(); }

bool PdfiumDocumentBackend::renderPages(const PipelineContext& context,
                                        std::vector<document::PageArtifact>& pages) const {
    const pdf::RenderService render;
    return render.renderPages(source_,
                              {
                                  context.render.dpi,
                                  context.output.root,
                                  context.output.pages_dir,
                              },
                              pages);
}

bool PdfiumDocumentBackend::extractNativeText(const PipelineContext& context,
                                              std::vector<document::PageText>& page_texts) const {
    const pdf::TextService text;
    return text.extractText(source_, context.render.dpi, page_texts);
}

} // namespace doc_parser::pipeline
