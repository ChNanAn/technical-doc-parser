#pragma once

#include "common/diagnostic.h"

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/text_model.h"

#include <document_intelligence_engine/engine_config.h>

#include <memory>
#include <string>
#include <vector>

namespace doc_parser::layout {

struct LayoutRequest {
    const document::PageArtifact& page;
    const document::PageText& text;
};

struct LayoutResult {
    document::PageLayout layout;
    std::vector<common::Diagnostic> diagnostics;
};

class ILayoutBackend {
public:
    virtual ~ILayoutBackend() = default;

    virtual bool analyze(const LayoutRequest& request, LayoutResult& result) const = 0;
    virtual bool isAvailable() const { return true; }
    virtual std::string unavailableReason() const { return {}; }
};

class TextLayoutModelBackend final : public ILayoutBackend {
public:
    bool analyze(const LayoutRequest& request, LayoutResult& result) const override;
};

document::LayoutBlockType mapDocLayNetLabel(const std::string& label);
document::LayoutBlockType mapPaddleDocLayoutLabel(const std::string& label);

class DocLayNetOnnxBackend final : public ILayoutBackend {
public:
    DocLayNetOnnxBackend();
    explicit DocLayNetOnnxBackend(DocLayNetOnnxConfig config);
    ~DocLayNetOnnxBackend() override;

    DocLayNetOnnxBackend(const DocLayNetOnnxBackend&) = delete;
    DocLayNetOnnxBackend& operator=(const DocLayNetOnnxBackend&) = delete;
    DocLayNetOnnxBackend(DocLayNetOnnxBackend&&) noexcept;
    DocLayNetOnnxBackend& operator=(DocLayNetOnnxBackend&&) noexcept;

    bool isAvailable() const override;
    std::string unavailableReason() const override;
    const DocLayNetOnnxConfig& config() const;
    bool analyze(const LayoutRequest& request, LayoutResult& result) const override;

private:
    struct ModelBundle;
    static std::unique_ptr<ModelBundle> loadModel(const DocLayNetOnnxConfig& config);

    DocLayNetOnnxConfig config_;
    std::unique_ptr<ModelBundle> model_;
};

class PaddleDocLayoutOnnxBackend final : public ILayoutBackend {
public:
    PaddleDocLayoutOnnxBackend();
    explicit PaddleDocLayoutOnnxBackend(PaddleDocLayoutOnnxConfig config);
    ~PaddleDocLayoutOnnxBackend() override;

    PaddleDocLayoutOnnxBackend(const PaddleDocLayoutOnnxBackend&) = delete;
    PaddleDocLayoutOnnxBackend& operator=(const PaddleDocLayoutOnnxBackend&) = delete;
    PaddleDocLayoutOnnxBackend(PaddleDocLayoutOnnxBackend&&) noexcept;
    PaddleDocLayoutOnnxBackend& operator=(PaddleDocLayoutOnnxBackend&&) noexcept;

    bool isAvailable() const override;
    std::string unavailableReason() const override;
    const PaddleDocLayoutOnnxConfig& config() const;
    bool analyze(const LayoutRequest& request, LayoutResult& result) const override;

private:
    struct ModelBundle;
    static std::unique_ptr<ModelBundle> loadModel(const PaddleDocLayoutOnnxConfig& config);

    PaddleDocLayoutOnnxConfig config_;
    std::unique_ptr<ModelBundle> model_;
};

} // namespace doc_parser::layout
