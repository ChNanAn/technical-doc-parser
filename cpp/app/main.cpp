#include "app/cli_options.h"
#include "export/document_exporter.h"
#include "pipeline/document_engine.h"

#include <CLI/CLI.hpp>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <utility>

namespace {

doc_parser::pipeline::DocumentParseOptions parseOptions(const doc_parser::app::CliOptions& options) {
    doc_parser::pipeline::DocumentParseOptions parse_options;
    parse_options.input_path = options.input_pdf;
    parse_options.output_directory = options.output_dir;
    parse_options.render.dpi = options.dpi;
    parse_options.debug = options.debug;
    parse_options.timeout_seconds = options.timeout_seconds;
    parse_options.maximum_pages = options.maximum_pages;
    return parse_options;
}

doc_parser::pipeline::BackendOptions backendOptions(const doc_parser::app::CliOptions& options) {
    doc_parser::pipeline::BackendOptions backends;
    backends.document = options.document_backend;
    backends.ocr = options.ocr_backend;
    backends.layout = options.layout_backend;
    backends.table = options.table_backend;
    backends.registry_config = options.backend_config;
    return backends;
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

    const doc_parser::pipeline::BackendOptions backends = backendOptions(options);
    doc_parser::pipeline::DocumentParseOptions parse_options = parseOptions(options);
    doc_parser::pipeline::DocumentEngine engine(doc_parser::pipeline::engineConfigFromEnvironment(backends));
    if (!engine.isReady()) {
        const doc_parser::common::Status& status = engine.initializationStatus();
        spdlog::error("failed to initialize document engine [{}]: {}", status.code(), status.message());
        return 2;
    }
    doc_parser::pipeline::ParseResult result = engine.parse(parse_options);
    if (!result.ok()) {
        const doc_parser::common::Status& status = result.status;
        spdlog::error("document parsing failed at {} [{}]: {}", status.stage(), status.code(), status.message());
        return 2;
    }

    const auto document_exporter = doc_parser::exporter::createDefaultDocumentExporter();
    const std::filesystem::path output_path = parse_options.output_directory / "document.json";
    if (document_exporter == nullptr) {
        spdlog::error("failed to export parsed document: no document exporter is enabled");
        return 2;
    }
    const doc_parser::common::Status export_status =
        document_exporter->write({parse_options.debug, output_path, &result.document, &result.artifacts});
    if (!export_status.okStatus()) {
        spdlog::error("document export failed at {} [{}]: {}",
                      export_status.stage(),
                      export_status.code(),
                      export_status.message());
        return 2;
    }

    return EXIT_SUCCESS;
}
