#pragma once

#include "ocr/ocr_backend.h"

#include <filesystem>
#include <memory>

namespace doc_parser::ocr {

struct PaddleOcrOnnxConfig {
    std::filesystem::path detection_model;
    std::filesystem::path recognition_model;
    std::filesystem::path angle_classifier_model;
    std::filesystem::path character_dict;
    int detection_limit_side = 960;
    int recognition_image_height = 48;
    int recognition_image_width = 320;
    double detection_threshold = 0.3;
    double box_threshold = 0.5;
    double recognition_threshold = 0.1;
    double unclip_ratio = 1.5;
    bool enable_angle_classifier = false;
};

class PaddleOcrOnnxBackend final : public IOcrBackend {
public:
    PaddleOcrOnnxBackend();
    explicit PaddleOcrOnnxBackend(PaddleOcrOnnxConfig config);
    ~PaddleOcrOnnxBackend() override;

    PaddleOcrOnnxBackend(PaddleOcrOnnxBackend&&) noexcept;
    PaddleOcrOnnxBackend& operator=(PaddleOcrOnnxBackend&&) noexcept;

    bool isAvailable() const;
    bool recognize(const OcrRequest& request, OcrResult& result) const override;

private:
    struct ModelBundle;
    static std::unique_ptr<ModelBundle> loadModelBundle(const PaddleOcrOnnxConfig& config);

    PaddleOcrOnnxConfig config_;
    std::unique_ptr<ModelBundle> model_;
};

} // namespace doc_parser::ocr
