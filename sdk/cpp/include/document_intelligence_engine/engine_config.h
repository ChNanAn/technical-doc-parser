#pragma once

#include "document_intelligence_engine/options.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>

#ifndef DOC_PARSER_PADDLEOCR_BASELINE_DIR
#define DOC_PARSER_PADDLEOCR_BASELINE_DIR "models/paddleocr/baseline"
#endif

#ifndef DOC_PARSER_DOCLAYNET_MODEL_PATH
#define DOC_PARSER_DOCLAYNET_MODEL_PATH "models/layout/doclaynet/model.onnx"
#endif

#ifndef DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH
#define DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH "models/layout/paddle/pp-doclayout-v3.onnx"
#endif

#ifndef DOC_PARSER_TABLE_DETECTION_MODEL_PATH
#define DOC_PARSER_TABLE_DETECTION_MODEL_PATH "models/table/table-transformer/detection.onnx"
#endif

#ifndef DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH
#define DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH "models/table/table-transformer/structure.onnx"
#endif

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

} // namespace doc_parser::ocr

namespace doc_parser::layout {

struct DocLayNetOnnxConfig {
    std::filesystem::path model_path;
    int input_width = 576;
    int input_height = 576;
    double confidence_threshold = 0.5;
};

struct PaddleDocLayoutOnnxConfig {
    std::filesystem::path model_path;
    int input_width = 800;
    int input_height = 800;
    double confidence_threshold = 0.5;
};

} // namespace doc_parser::layout

namespace doc_parser::table {

struct TableTransformerOnnxConfig {
    std::filesystem::path detection_model_path;
    std::filesystem::path structure_model_path;
    double detection_confidence_threshold = 0.9;
    double structure_confidence_threshold = 0.5;
    int crop_padding = 20;
};

} // namespace doc_parser::table

namespace doc_parser::pipeline {

struct TesseractConfig {
    std::string executable = "tesseract";
    std::string language = "eng";
};

struct EngineConfig {
    BackendOptions backends;
    TesseractConfig tesseract;
    ocr::PaddleOcrOnnxConfig paddle_ocr;
    layout::DocLayNetOnnxConfig doclaynet;
    layout::PaddleDocLayoutOnnxConfig paddle_layout;
    table::TableTransformerOnnxConfig table_transformer;
};

// Build-tree consumers use relocatable defaults; installed packages inject their
// model prefix per consumer. Internal linkage keeps those definitions independent.
static inline EngineConfig defaultEngineConfig() {
    EngineConfig config;
    const std::filesystem::path paddle_ocr_dir = DOC_PARSER_PADDLEOCR_BASELINE_DIR;
    config.paddle_ocr.detection_model = paddle_ocr_dir / "det.onnx";
    config.paddle_ocr.recognition_model = paddle_ocr_dir / "rec.onnx";
    config.paddle_ocr.character_dict = paddle_ocr_dir / "ppocrv5_dict.txt";
    config.doclaynet.model_path = DOC_PARSER_DOCLAYNET_MODEL_PATH;
    config.paddle_layout.model_path = DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH;
    config.table_transformer.detection_model_path = DOC_PARSER_TABLE_DETECTION_MODEL_PATH;
    config.table_transformer.structure_model_path = DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH;
    return config;
}

// Compatibility adapter for process entry points such as the CLI and Worker.
// The engine itself never reads model configuration from the environment.
EngineConfig engineConfigFromEnvironment(EngineConfig config);

static inline EngineConfig engineConfigFromEnvironment(BackendOptions backends = {}) {
    EngineConfig config = defaultEngineConfig();
    config.backends = std::move(backends);
    return engineConfigFromEnvironment(std::move(config));
}

} // namespace doc_parser::pipeline
