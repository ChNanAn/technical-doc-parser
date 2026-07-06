#pragma once

#include "layout/layout_service.h"
#include "ocr/ocr_service.h"
#include "pipeline/pipeline_context.h"
#include "table/table_service.h"

#include <memory>
#include <string>

namespace doc_parser::pipeline {

struct PipelineServices {
    std::unique_ptr<ocr::OcrService> ocr;
    std::unique_ptr<layout::LayoutService> layout;
    std::unique_ptr<table::TableService> table;
};

struct BackendSelectionResult {
    bool ok = false;
    std::string error_stage;
    std::string error_message;
    std::string trace_message;
    PipelineServices services;
};

BackendSelectionResult createPipelineServices(const BackendOptions& options);

} // namespace doc_parser::pipeline
