#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"
#include "pipeline/pipeline_context.h"

#include <filesystem>
#include <string>
#include <vector>

namespace doc_parser::pipeline {

struct DocumentBackendCapabilities {
    bool can_render_pages = true;
    bool can_extract_native_text = true;
};

class IDocumentBackend {
public:
    virtual ~IDocumentBackend() = default;

    virtual bool open(const std::filesystem::path& input_path) = 0;
    virtual std::string sourcePath() const = 0;
    virtual std::string sourceType() const = 0;
    virtual int pageCount() const = 0;
    virtual DocumentBackendCapabilities capabilities() const { return {}; }

    virtual bool renderPages(const PipelineContext& context, std::vector<document::PageArtifact>& pages) const = 0;

    virtual bool extractNativeText(const PipelineContext& context,
                                   std::vector<document::PageText>& page_texts) const = 0;
};

} // namespace doc_parser::pipeline
