#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/text_model.h"

#include <filesystem>
#include <memory>
#include <string>

namespace doc_parser::layout {

struct LayoutRequest {
    const document::PageArtifact& page;
    const document::PageText& text;
};

struct LayoutResult {
    document::PageLayout layout;
};

class ILayoutBackend {
public:
    virtual ~ILayoutBackend() = default;

    virtual bool analyze(const LayoutRequest& request, LayoutResult& result) const = 0;
};

class TextLayoutModelBackend final : public ILayoutBackend {
public:
    bool analyze(const LayoutRequest& request, LayoutResult& result) const override;
};

document::LayoutBlockType mapDocLayNetLabel(const std::string& label);
document::LayoutBlockType mapPaddleDocLayoutLabel(const std::string& label);

struct DocLayNetOnnxConfig {
    std::filesystem::path model_path;
    int input_width = 576;
    int input_height = 576;
    double confidence_threshold = 0.5;
};

class DocLayNetOnnxBackend final : public ILayoutBackend {
public:
    DocLayNetOnnxBackend();
    explicit DocLayNetOnnxBackend(DocLayNetOnnxConfig config);
    ~DocLayNetOnnxBackend() override;

    DocLayNetOnnxBackend(const DocLayNetOnnxBackend&) = delete;
    DocLayNetOnnxBackend& operator=(const DocLayNetOnnxBackend&) = delete;
    DocLayNetOnnxBackend(DocLayNetOnnxBackend&&) noexcept;
    DocLayNetOnnxBackend& operator=(DocLayNetOnnxBackend&&) noexcept;

    bool isAvailable() const;
    const DocLayNetOnnxConfig& config() const;
    bool analyze(const LayoutRequest& request, LayoutResult& result) const override;

private:
    struct ModelBundle;
    static std::unique_ptr<ModelBundle> loadModel(const DocLayNetOnnxConfig& config);

    DocLayNetOnnxConfig config_;
    std::unique_ptr<ModelBundle> model_;
};

struct PaddleDocLayoutOnnxConfig {
    std::filesystem::path model_path;
    int input_width = 800;
    int input_height = 800;
    double confidence_threshold = 0.5;
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

    bool isAvailable() const;
    const PaddleDocLayoutOnnxConfig& config() const;
    bool analyze(const LayoutRequest& request, LayoutResult& result) const override;

private:
    struct ModelBundle;
    static std::unique_ptr<ModelBundle> loadModel(const PaddleDocLayoutOnnxConfig& config);

    PaddleDocLayoutOnnxConfig config_;
    std::unique_ptr<ModelBundle> model_;
};

} // namespace doc_parser::layout
