#include "app/cli_options.h"
#include "pipeline/document_pipeline.h"

#include <CLI/CLI.hpp>
#include <cstdlib>
#include <spdlog/spdlog.h>

namespace {

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
    app.add_option("--document-backend", options.document_backend, "Document backend: auto, pdf");
    app.add_option("--ocr-backend", options.ocr_backend, "OCR backend: auto, tesseract, paddle, noop");
    app.add_option("--layout-backend", options.layout_backend, "Layout backend: auto, text");
    app.add_option("--table-backend", options.table_backend, "Table backend: auto, text");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    configureLogging(options);

    const doc_parser::pipeline::DocumentPipeline pipeline;
    if (!pipeline.run(options)) {
        return 2;
    }

    return EXIT_SUCCESS;
}
