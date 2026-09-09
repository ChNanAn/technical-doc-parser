#pragma once

#include "document_source/document_source_interfaces.h"
#include "document_source/pdf/pdf_document.h"

namespace doc_parser::document_source::pdf {

class PdfDocumentBackend final : public IDocumentSource, public IPageRenderer, public INativeTextExtractor {
public:
    bool open(const std::filesystem::path& input_path) override;
    std::string sourcePath() const override;
    std::string sourceType() const override;
    int pageCount() const override;

    bool renderPages(const RenderRequest& request, std::vector<document::PageArtifact>& pages) const override;
    bool supportsPageRendering() const override { return true; }
    bool renderPage(const RenderRequest& request, int page_index, document::PageArtifact& page) const override;

    bool extractNativeText(const NativeTextRequest& request,
                           std::vector<document::PageText>& page_texts) const override;
    bool supportsPageTextExtraction() const override { return true; }
    bool extractPageNativeText(const NativeTextRequest& request,
                               int page_index,
                               document::PageText& page_text) const override;

private:
    std::string source_path_;
    doc_parser::pdf::PdfDocument source_;
};

} // namespace doc_parser::document_source::pdf
