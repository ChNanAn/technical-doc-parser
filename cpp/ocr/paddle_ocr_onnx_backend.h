#pragma once

#include "ocr/ocr_backend.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace doc_parser::ocr {

struct PaddleOcrModelProfile {
    std::string name = "ppocrv5_mobile";
    std::array<float, 3> detection_mean{0.485F, 0.456F, 0.406F};
    std::array<float, 3> detection_std{0.229F, 0.224F, 0.225F};
    float detection_scale = 1.0F / 255.0F;
    std::array<float, 3> recognition_mean{0.5F, 0.5F, 0.5F};
    std::array<float, 3> recognition_std{0.5F, 0.5F, 0.5F};
    float recognition_scale = 1.0F / 255.0F;
    bool convert_bgr_to_rgb = false;
};

struct PaddleOcrOnnxConfig {
    std::filesystem::path detection_model;
    std::filesystem::path recognition_model;
    std::filesystem::path character_dict;
    PaddleOcrModelProfile profile;
    int detection_limit_side = 960;
    int recognition_image_height = 48;
    int recognition_base_width = 320;
    int recognition_max_width = 2048;
    int recognition_width_multiple = 8;
    std::size_t recognition_batch_size = 8;
    std::size_t detection_max_candidates = 1000;
    double detection_threshold = 0.3;
    double box_threshold = 0.5;
    double recognition_threshold = 0.1;
    double unclip_ratio = 1.5;
};

class PaddleOcrOnnxBackend final : public IOcrBackend {
public:
    PaddleOcrOnnxBackend();
    explicit PaddleOcrOnnxBackend(PaddleOcrOnnxConfig config);
    ~PaddleOcrOnnxBackend() override;

    PaddleOcrOnnxBackend(PaddleOcrOnnxBackend&&) noexcept;
    PaddleOcrOnnxBackend& operator=(PaddleOcrOnnxBackend&&) noexcept;

    bool isAvailable() const;
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
