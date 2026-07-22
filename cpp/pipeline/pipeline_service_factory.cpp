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
#include <string>
#include <utility>
#include <vector>

namespace doc_parser::pipeline {
namespace {

struct AutoOcrSelection {
    std::unique_ptr<ocr::IOcrBackend> backend;
    std::string name;
    bool available = false;
};

class ChainedLayoutBackend final : public layout::ILayoutBackend {
public:
    struct Entry {
        std::string name;
        std::unique_ptr<layout::ILayoutBackend> backend;
    };

    explicit ChainedLayoutBackend(std::vector<Entry> backends) : backends_(std::move(backends)) {}

    bool analyze(const layout::LayoutRequest& request, layout::LayoutResult& result) const override {
        for (std::size_t index = 0; index < backends_.size(); ++index) {
            if (backends_[index].backend->analyze(request, result)) {
                return true;
            }
            if (index + 1 < backends_.size()) {
                spdlog::warn("layout: {} inference failed for page {}; falling back to {}",
                             backends_[index].name,
                             request.page.page_number,
                             backends_[index + 1].name);
            }
        }
        return false;
    }

private:
    std::vector<Entry> backends_;
};

AutoOcrSelection createAutoOcrBackend() {
#if DOC_PARSER_ENABLE_ONNXRUNTIME
    auto paddle = std::make_unique<ocr::PaddleOcrOnnxBackend>();
    if (paddle->isAvailable()) {
        return {std::move(paddle), "paddle", true};
    }
#endif

    auto tesseract = std::make_unique<ocr::TesseractCliOcrBackend>();
    if (tesseract->isAvailable()) {
        return {std::move(tesseract), "tesseract", true};
    }

    return {
        std::make_unique<ocr::UnavailableOcrBackend>("no OCR backend is available; install models or select "
                                                     "--ocr-backend noop explicitly"),
        "unavailable",
        false,
    };
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
    std::string selected_ocr = options.ocr;
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
        AutoOcrSelection selection = createAutoOcrBackend();
        selected_ocr = selection.name;
        if (!selection.available) {
            spdlog::warn("configure_ocr_backend: auto mode found no usable OCR backend; native-text documents can "
                         "still be processed, but pages requiring OCR will fail");
        }
        ocr_backend = std::move(selection.backend);
    } else {
        spdlog::error("configure_ocr_backend: unknown OCR backend: {}", options.ocr);
        result.error_stage = "configure_ocr_backend";
        result.error_message = "unknown OCR backend: " + options.ocr;
        return result;
    }

    std::unique_ptr<layout::ILayoutBackend> layout_backend;
    std::string selected_layout = options.layout;
    if (options.layout == "text") {
        layout_backend = std::make_unique<layout::TextLayoutModelBackend>();
#if DOC_PARSER_ENABLE_ONNXRUNTIME
    } else if (options.layout == "doclaynet") {
        auto doclaynet = std::make_unique<layout::DocLayNetOnnxBackend>();
        if (!doclaynet->isAvailable()) {
            spdlog::error("configure_layout_backend: DocLayNet ONNX backend is not available");
            result.error_stage = "configure_layout_backend";
            result.error_message = "DocLayNet ONNX backend is not available";
            return result;
        }
        layout_backend = std::move(doclaynet);
    } else if (options.layout == "paddle-layout") {
        auto paddle = std::make_unique<layout::PaddleDocLayoutOnnxBackend>();
        if (!paddle->isAvailable()) {
            spdlog::error("configure_layout_backend: Paddle PP-DocLayoutV3 ONNX backend is not available");
            result.error_stage = "configure_layout_backend";
            result.error_message = "Paddle PP-DocLayoutV3 ONNX backend is not available";
            return result;
        }
        layout_backend = std::move(paddle);
#else
    } else if (options.layout == "doclaynet" || options.layout == "paddle-layout") {
        spdlog::error("configure_layout_backend: requested ONNX layout backend was not enabled at build time");
        result.error_stage = "configure_layout_backend";
        result.error_message = "requested ONNX layout backend was not enabled at build time";
        return result;
#endif
    } else if (options.layout == "auto") {
#if DOC_PARSER_ENABLE_ONNXRUNTIME
        std::vector<ChainedLayoutBackend::Entry> backends;
        std::vector<std::string> selected_names;
        auto doclaynet = std::make_unique<layout::DocLayNetOnnxBackend>();
        if (doclaynet->isAvailable()) {
            selected_names.emplace_back("doclaynet");
            backends.push_back({"doclaynet", std::move(doclaynet)});
        }
        auto paddle = std::make_unique<layout::PaddleDocLayoutOnnxBackend>();
        if (paddle->isAvailable()) {
            selected_names.emplace_back("paddle-layout");
            backends.push_back({"paddle-layout", std::move(paddle)});
        }
        selected_names.emplace_back("text");
        backends.push_back({"text", std::make_unique<layout::TextLayoutModelBackend>()});
        selected_layout.clear();
        for (const std::string& name : selected_names) {
            selected_layout += (selected_layout.empty() ? "" : "->") + name;
        }
        if (selected_names.size() == 1) {
            spdlog::warn("configure_layout_backend: no ONNX layout model is available; using text-rule fallback");
        }
        layout_backend = std::make_unique<ChainedLayoutBackend>(std::move(backends));
#else
        selected_layout = "text(fallback:onnx-disabled)";
        layout_backend = std::make_unique<layout::TextLayoutModelBackend>();
#endif
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
    result.trace_message = "document=" + options.document + ", ocr=" + selected_ocr + ", layout=" + selected_layout +
                           ", table=" + options.table;
    result.ok = true;
    return result;
}

} // namespace doc_parser::pipeline
