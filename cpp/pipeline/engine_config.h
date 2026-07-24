#pragma once

#include "layout/layout_backend.h"
#include "ocr/paddle_ocr_onnx_backend.h"
#include "pipeline/pipeline_options.h"
#include "table/table_backend.h"

#include <string>

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

EngineConfig defaultEngineConfig();

// Compatibility adapter for process entry points such as the CLI and Worker.
// The engine itself never reads model configuration from the environment.
EngineConfig engineConfigFromEnvironment(BackendOptions backends = {});

} // namespace doc_parser::pipeline
