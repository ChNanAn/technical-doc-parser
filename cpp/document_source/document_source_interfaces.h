#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace doc_parser::document_source {

struct RenderRequest {
    int dpi = 200;
    std::filesystem::path output_root;
    std::filesystem::path pages_dir;
    // Optional synchronous handoff after the PNG has been written successfully.
    // The bitmap must describe the same pixels and dimensions as the artifact.
    // Consumers may move pixels out; renderers must not retain/invoke the callback
    // after rendering returns. Backends may ignore it and keep the file-only path.
    std::function<void(const document::PageArtifact&, document::PageBitmap&&)> on_page_rendered{};
};

struct NativeTextRequest {
    int dpi = 200;
};

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
    virtual bool renderPages(const RenderRequest& request, std::vector<document::PageArtifact>& pages) const = 0;

    // Optional capability. Legacy backends keep their whole-document path.
    virtual bool supportsPageRendering() const { return false; }
    virtual bool renderPage(const RenderRequest&, int, document::PageArtifact&) const { return false; }
};

class INativeTextExtractor {
public:
    virtual ~INativeTextExtractor() = default;
    virtual bool extractNativeText(const NativeTextRequest& request,
                                   std::vector<document::PageText>& page_texts) const = 0;

    virtual bool supportsPageTextExtraction() const { return false; }
    virtual bool extractPageNativeText(const NativeTextRequest&, int, document::PageText&) const { return false; }
};

} // namespace doc_parser::document_source
