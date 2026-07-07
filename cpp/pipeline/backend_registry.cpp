#include "pipeline/backend_registry.h"

#include "layout/layout_backend.h"
#include "ocr/ocr_backend.h"
#include "ocr/tesseract_cli_ocr_backend.h"
#include "reading_order/reading_order_backend.h"
#include "table/table_backend.h"

#include <memory>
#include <utility>

namespace doc_parser::pipeline {
namespace {

BackendSelectionResult failure(std::string stage, std::string message) {
    BackendSelectionResult result;
    result.error_stage = std::move(stage);
    result.error_message = std::move(message);
    return result;
}

} // namespace

BackendSelectionResult createPipelineServices(const BackendOptions& options) {
    BackendSelectionResult result;

    std::unique_ptr<ocr::IOcrBackend> ocr_backend;
    if (options.ocr == "noop") {
        ocr_backend = std::make_unique<ocr::NoopOcrBackend>();
    } else if (options.ocr == "tesseract") {
        auto tesseract = std::make_unique<ocr::TesseractCliOcrBackend>();
        if (!tesseract->isAvailable()) {
            return failure("configure_ocr_backend", "tesseract OCR backend is not available");
        }
        ocr_backend = std::move(tesseract);
    } else if (options.ocr == "auto") {
        auto tesseract = std::make_unique<ocr::TesseractCliOcrBackend>();
        if (tesseract->isAvailable()) {
            ocr_backend = std::move(tesseract);
        } else {
            ocr_backend = std::make_unique<ocr::NoopOcrBackend>();
        }
    } else {
        return failure("configure_ocr_backend", "unknown OCR backend: " + options.ocr);
    }

    std::unique_ptr<layout::ILayoutBackend> layout_backend;
    if (options.layout == "auto" || options.layout == "text") {
        layout_backend = std::make_unique<layout::TextLayoutModelBackend>();
    } else {
        return failure("configure_layout_backend", "unknown layout backend: " + options.layout);
    }

    std::unique_ptr<table::ITableBackend> table_backend;
    if (options.table == "auto" || options.table == "text") {
        table_backend = std::make_unique<table::TextTableStructureBackend>();
    } else {
        return failure("configure_table_backend", "unknown table backend: " + options.table);
    }

    result.services.ocr = std::make_unique<ocr::OcrService>(std::move(ocr_backend));
    result.services.layout = std::make_unique<layout::LayoutService>(std::move(layout_backend));
    result.services.reading_order = std::make_unique<reading_order::ReadingOrderService>(
        std::make_unique<reading_order::DoclingLikeReadingOrderBackend>());
    result.services.table = std::make_unique<table::TableService>(std::move(table_backend));
    result.trace_message = "document=" + options.document + ", ocr=" + options.ocr + ", layout=" + options.layout +
                           ", table=" + options.table;
    result.ok = true;
    return result;
}

} // namespace doc_parser::pipeline
