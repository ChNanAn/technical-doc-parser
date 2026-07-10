#include "pipeline/pipeline_service_factory.h"

#include "document_source/document_source_factory.h"
#include "layout/layout_backend.h"
#include "ocr/ocr_backend.h"
#if DOC_PARSER_ENABLE_ONNXRUNTIME
#include "ocr/paddle_ocr_onnx_backend.h"
#endif
#include "ocr/tesseract_cli_ocr_backend.h"
#include "reading_order/reading_order_backend.h"
#include "table/table_backend.h"

#include <memory>
#include <spdlog/spdlog.h>

namespace doc_parser::pipeline {
namespace {

std::unique_ptr<ocr::IOcrBackend> createAutoOcrBackend() {
    auto tesseract = std::make_unique<ocr::TesseractCliOcrBackend>();
    if (tesseract->isAvailable()) {
        return tesseract;
    }

#if DOC_PARSER_ENABLE_ONNXRUNTIME
    auto paddle = std::make_unique<ocr::PaddleOcrOnnxBackend>();
    if (paddle->isAvailable()) {
        return paddle;
    }
#endif

    return std::make_unique<ocr::NoopOcrBackend>();
}

} // namespace

PipelineServiceCreationResult createPipelineServices(const BackendOptions& options) {
    PipelineServiceCreationResult result;

    result.services.document = document_source::createDocumentSource(options.document);
    if (result.services.document.source == nullptr) {
        spdlog::error("configure_document_source: no matching document source is enabled: {}", options.document);
        result.error_stage = "configure_document_source";
        result.error_message = "no matching document source is enabled: " + options.document;
        return result;
    }

    std::unique_ptr<ocr::IOcrBackend> ocr_backend;
    if (options.ocr == "noop") {
        ocr_backend = std::make_unique<ocr::NoopOcrBackend>();
    } else if (options.ocr == "tesseract") {
        auto tesseract = std::make_unique<ocr::TesseractCliOcrBackend>();
        if (!tesseract->isAvailable()) {
            spdlog::error("configure_ocr_backend: tesseract OCR backend is not available");
            result.error_stage = "configure_ocr_backend";
            result.error_message = "tesseract OCR backend is not available";
            return result;
        }
        ocr_backend = std::move(tesseract);
#if DOC_PARSER_ENABLE_ONNXRUNTIME
    } else if (options.ocr == "paddle") {
        auto paddle = std::make_unique<ocr::PaddleOcrOnnxBackend>();
        if (!paddle->isAvailable()) {
            spdlog::error("configure_ocr_backend: PaddleOCR ONNX backend is not available");
            result.error_stage = "configure_ocr_backend";
            result.error_message = "PaddleOCR ONNX backend is not available";
            return result;
        }
        ocr_backend = std::move(paddle);
#else
    } else if (options.ocr == "paddle") {
        spdlog::error("configure_ocr_backend: PaddleOCR ONNX backend was not enabled at build time");
        result.error_stage = "configure_ocr_backend";
        result.error_message = "PaddleOCR ONNX backend was not enabled at build time";
        return result;
#endif
    } else if (options.ocr == "auto") {
        ocr_backend = createAutoOcrBackend();
    } else {
        spdlog::error("configure_ocr_backend: unknown OCR backend: {}", options.ocr);
        result.error_stage = "configure_ocr_backend";
        result.error_message = "unknown OCR backend: " + options.ocr;
        return result;
    }

    std::unique_ptr<layout::ILayoutBackend> layout_backend;
    if (options.layout == "auto" || options.layout == "text") {
        layout_backend = std::make_unique<layout::TextLayoutModelBackend>();
    } else {
        spdlog::error("configure_layout_backend: unknown layout backend: {}", options.layout);
        result.error_stage = "configure_layout_backend";
        result.error_message = "unknown layout backend: " + options.layout;
        return result;
    }

    std::unique_ptr<table::ITableBackend> table_backend;
    if (options.table == "auto" || options.table == "text") {
        table_backend = std::make_unique<table::TextTableStructureBackend>();
    } else {
        spdlog::error("configure_table_backend: unknown table backend: {}", options.table);
        result.error_stage = "configure_table_backend";
        result.error_message = "unknown table backend: " + options.table;
        return result;
    }

    result.services.ocr = std::move(ocr_backend);
    result.services.layout = std::move(layout_backend);
    result.services.reading_order = std::make_unique<reading_order::DoclingLikeReadingOrderBackend>();
    result.services.table = std::move(table_backend);
    result.trace_message = "document=" + options.document + ", ocr=" + options.ocr + ", layout=" + options.layout +
                           ", table=" + options.table;
    result.ok = true;
    return result;
}

} // namespace doc_parser::pipeline
