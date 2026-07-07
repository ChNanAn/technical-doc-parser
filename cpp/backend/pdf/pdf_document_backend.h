#pragma once

#include "backend/pdf/pdf_document.h"
#include "pipeline/stage_interfaces.h"

namespace doc_parser::backend::pdf {

class PdfDocumentBackend final : public pipeline::IDocumentSource,
                                 public pipeline::IPageRenderer,
                                 public pipeline::INativeTextExtractor {
public:
    bool open(const std::filesystem::path& input_path) override;
    std::string sourcePath() const override;
    std::string sourceType() const override;
    int pageCount() const override;

    bool renderPages(const pipeline::PipelineContext& context,
                     std::vector<document::PageArtifact>& pages) const override;

    bool extractNativeText(const pipeline::PipelineContext& context,
                           std::vector<document::PageText>& page_texts) const override;

private:
    std::string source_path_;
    doc_parser::pdf::PdfDocument source_;
};

} // namespace doc_parser::backend::pdf
