#include "app/cli_options.h"
#include "pipeline/document_pipeline.h"

#include <CLI/CLI.hpp>
#include <cstdlib>

int main(int argc, char** argv) {
    doc_parser::app::CliOptions options;

    CLI::App app{"Technical Doc Parser"};
    app.add_option("input_pdf", options.input_pdf, "Input PDF file")->required()->check(CLI::ExistingFile);
    app.add_option("-o,--out", options.output_dir, "Output directory");
    app.add_option("--dpi", options.dpi, "Render DPI")->check(CLI::PositiveNumber);
    app.add_flag("--debug", options.debug, "Write intermediate debug files");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    const doc_parser::pipeline::DocumentPipeline pipeline;
    if (!pipeline.run(options)) {
        return 2;
    }

    return EXIT_SUCCESS;
}
