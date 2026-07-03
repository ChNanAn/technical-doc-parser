#pragma once

#include "pdf/pdf_document.h"
#include "pipeline/stage_interfaces.h"

namespace doc_parser::pipeline {

class PdfiumDocumentBackend final : public IDocumentBackend {
public:
    bool open(const std::filesystem::path& input_path) override;
    std::string sourcePath() const override;
    std::string sourceType() const override;
    int pageCount() const override;

    bool renderPages(const PipelineContext& context, std::vector<document::PageArtifact>& pages) const override;

    bool extractNativeText(const PipelineContext& context, std::vector<document::PageText>& page_texts) const override;

private:
    std::string source_path_;
    pdf::PdfDocument source_;
};

} // namespace doc_parser::pipeline
