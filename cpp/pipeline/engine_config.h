#pragma once

#include "layout/layout_backend.h"
#include "ocr/paddle_ocr_onnx_backend.h"
#include "pipeline/pipeline_options.h"
#include "table/table_backend.h"

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

// Package model paths are injected into each consumer; internal linkage prevents
// installed consumers and the static library from defining one function differently.
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
