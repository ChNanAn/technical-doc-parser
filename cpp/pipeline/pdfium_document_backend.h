#pragma once

#include "pdf/pdf_document.h"
#include "pipeline/stage_interfaces.h"
#include "pipeline/text_extraction_stage.h"

namespace doc_parser::pipeline {

class PdfiumDocumentBackend final : public IDocumentBackend {
public:
    bool open(const std::filesystem::path& input_path) override;
    std::string sourcePath() const override;
    std::string sourceType() const override;
    int pageCount() const override;

    bool renderPages(const PipelineContext& context, std::vector<document::PageArtifact>& pages) const override;

    bool extractText(const PipelineContext& context,
                     const std::vector<document::PageArtifact>& pages,
                     std::vector<document::PageText>& page_texts) const override;

private:
    std::string source_path_;
    pdf::PdfDocument source_;
    TextExtractionStage text_extraction_;
};

} // namespace doc_parser::pipeline
