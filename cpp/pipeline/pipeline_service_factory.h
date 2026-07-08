#pragma once

#include "layout/layout_backend.h"
#include "ocr/ocr_backend.h"
#include "pipeline/pipeline_context.h"
#include "reading_order/reading_order_backend.h"
#include "table/table_backend.h"

#include <memory>
#include <string>

namespace doc_parser::pipeline {

struct PipelineServices {
    std::unique_ptr<ocr::IOcrBackend> ocr;
    std::unique_ptr<layout::ILayoutBackend> layout;
    std::unique_ptr<reading_order::IReadingOrderBackend> reading_order;
    std::unique_ptr<table::ITableBackend> table;
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
