#include "app/cli_options.h"
#include "export/document_exporter.h"
#include "pipeline/document_engine.h"

#include <CLI/CLI.hpp>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <utility>

namespace {

doc_parser::pipeline::PipelineRunOptions pipelineOptions(const doc_parser::app::CliOptions& options) {
    doc_parser::pipeline::PipelineRunOptions pipeline_options;
    pipeline_options.input_path = options.input_pdf;
    pipeline_options.output_directory = options.output_dir;
    pipeline_options.render.dpi = options.dpi;
    pipeline_options.backends.document = options.document_backend;
    pipeline_options.backends.ocr = options.ocr_backend;
    pipeline_options.backends.layout = options.layout_backend;
    pipeline_options.backends.table = options.table_backend;
    pipeline_options.backends.registry_config = options.backend_config;
    pipeline_options.debug = options.debug;
    pipeline_options.timeout_seconds = options.timeout_seconds;
    pipeline_options.maximum_pages = options.maximum_pages;
    return pipeline_options;
}

void configureLogging(const doc_parser::app::CliOptions& options) {
    spdlog::set_pattern("[%l] %v");
    spdlog::set_level(options.debug ? spdlog::level::debug : spdlog::level::warn);
}

} // namespace

int main(int argc, char** argv) {
    doc_parser::app::CliOptions options;

    CLI::App app{"Document Intelligence Engine"};
    app.add_option("input_pdf", options.input_pdf, "Input PDF file")->required()->check(CLI::ExistingFile);
    app.add_option("-o,--out", options.output_dir, "Output directory");
    app.add_option("--dpi", options.dpi, "Render DPI")->check(CLI::PositiveNumber);
    app.add_flag("--debug", options.debug, "Write intermediate debug files");
    app.add_option("--document-backend", options.document_backend, "Document source: auto, pdf");
    app.add_option("--ocr-backend", options.ocr_backend, "OCR backend: auto, tesseract, paddle, noop");
    app.add_option("--layout-backend", options.layout_backend, "Layout backend: auto, doclaynet, paddle-layout, text");
    app.add_option("--table-backend", options.table_backend, "Table backend: auto, table-transformer, text");
    app.add_option("--backend-config", options.backend_config, "Backend registry JSON configuration")
        ->check(CLI::ExistingFile);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    configureLogging(options);

    doc_parser::pipeline::PipelineRunOptions pipeline_options = pipelineOptions(options);
    doc_parser::pipeline::DocumentEngine engine(
        doc_parser::pipeline::engineConfigFromEnvironment(pipeline_options.backends));
    if (!engine.isReady()) {
        const doc_parser::common::Status& status = engine.initializationStatus();
        spdlog::error("failed to initialize document engine [{}]: {}", status.code(), status.message());
        return 2;
    }
    doc_parser::pipeline::ParseResult result = engine.parse(pipeline_options);
    if (!result.ok()) {
        const doc_parser::common::Status& status = result.status;
        spdlog::error("document parsing failed at {} [{}]: {}", status.stage(), status.code(), status.message());
        return 2;
    }

    const auto document_exporter = doc_parser::exporter::createDefaultDocumentExporter();
    const std::filesystem::path output_path = pipeline_options.output_directory / "document.json";
    if (document_exporter == nullptr ||
        !document_exporter->write({pipeline_options.debug, output_path, &result.document, &result.artifacts})) {
        spdlog::error("failed to export parsed document to {}", output_path.string());
        return 2;
    }

    return EXIT_SUCCESS;
}
