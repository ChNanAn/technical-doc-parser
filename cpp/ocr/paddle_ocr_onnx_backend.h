#pragma once

#include "ocr/ocr_backend.h"

#include <document_intelligence_engine/engine_config.h>

#include <memory>
#include <string>

namespace doc_parser::ocr {

class PaddleOcrOnnxBackend final : public IOcrBackend {
public:
    PaddleOcrOnnxBackend();
    explicit PaddleOcrOnnxBackend(PaddleOcrOnnxConfig config);
    ~PaddleOcrOnnxBackend() override;

    PaddleOcrOnnxBackend(PaddleOcrOnnxBackend&&) noexcept;
    PaddleOcrOnnxBackend& operator=(PaddleOcrOnnxBackend&&) noexcept;

    bool isAvailable() const override;
    std::string unavailableReason() const override;
    const PaddleOcrOnnxConfig& config() const;
    bool recognize(const OcrRequest& request, OcrResult& result) const override;
    bool detect(const OcrRequest& request, OcrDetectionResult& result) const override;
    bool recognizeRegions(const OcrRegionRequest& request, OcrRegionRecognitionResult& result) const override;

private:
    struct ModelBundle;
    static std::unique_ptr<ModelBundle> loadModelBundle(const PaddleOcrOnnxConfig& config);

    PaddleOcrOnnxConfig config_;
    std::unique_ptr<ModelBundle> model_;
};

} // namespace doc_parser::ocr
