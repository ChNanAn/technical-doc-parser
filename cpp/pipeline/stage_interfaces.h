#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"
#include "pipeline/pipeline_context.h"

#include <filesystem>
#include <string>
#include <vector>

namespace doc_parser::pipeline {

class IDocumentSource {
public:
    virtual ~IDocumentSource() = default;

    virtual bool open(const std::filesystem::path& input_path) = 0;
    virtual std::string sourcePath() const = 0;
    virtual std::string sourceType() const = 0;
    virtual int pageCount() const = 0;
};

class IPageRenderer {
public:
    virtual ~IPageRenderer() = default;
    virtual bool renderPages(const PipelineContext& context, std::vector<document::PageArtifact>& pages) const = 0;
};

class INativeTextExtractor {
public:
    virtual ~INativeTextExtractor() = default;
    virtual bool extractNativeText(const PipelineContext& context,
                                   std::vector<document::PageText>& page_texts) const = 0;
};

} // namespace doc_parser::pipeline
