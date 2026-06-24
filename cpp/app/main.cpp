#include <CLI/CLI.hpp>

#include <iostream>
#include <string>

struct CliOptions {
    std::string input_pdf;
    std::string output_dir = "output";
    int dpi = 200;
    bool debug = false;
};

int main(int argc, char** argv) {
    CliOptions options;

    CLI::App app{"Technical Doc Parser"};
    app.add_option("input_pdf", options.input_pdf, "Input PDF file")
        ->required()
        ->check(CLI::ExistingFile);
    app.add_option("-o,--out", options.output_dir, "Output directory");
    app.add_option("--dpi", options.dpi, "Render DPI")
        ->check(CLI::PositiveNumber);
    app.add_flag("--debug", options.debug, "Write intermediate debug files");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    std::cout << "input_pdf: " << options.input_pdf << '\n'
              << "output_dir: " << options.output_dir << '\n'
              << "dpi: " << options.dpi << '\n'
              << "debug: " << (options.debug ? "true" : "false") << '\n';

    return 0;
}
