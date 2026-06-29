#include <CLI/CLI.hpp>

#if DOC_PARSER_ENABLE_PDFIUM
#include "pdf/pdf_library.h"
#include "pdf/pdf_reader.h"
#endif

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
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

#if DOC_PARSER_ENABLE_PDFIUM
    doc_parser::pdf::PdfLibrary pdf_library;
    doc_parser::pdf::PdfReader reader;

    if (!reader.open(options.input_pdf)) {
        std::cerr << "error: failed to open PDF: " << options.input_pdf << '\n';
        return 2;
    }

    const int pages = reader.pageCount();
    std::cout << "input_pdf: " << options.input_pdf << '\n'
              << "output_dir: " << options.output_dir << '\n'
              << "dpi: " << options.dpi << '\n'
              << "debug: " << (options.debug ? "true" : "false") << '\n'
              << "pages: " << pages << '\n';

    const std::filesystem::path output_dir(options.output_dir);
    const std::filesystem::path pages_dir = output_dir / "pages";
    std::error_code ec;
    std::filesystem::create_directories(pages_dir, ec);
    if (ec) {
        std::cerr << "error: failed to create output directory: " << pages_dir << '\n';
        return 2;
    }

    nlohmann::json manifest;
    manifest["source"] = {
        {"path", options.input_pdf},
        {"type", "pdf"},
    };
    manifest["render"] = {
        {"dpi", options.dpi},
    };
    manifest["pages"] = nlohmann::json::array();

    for (int page_index = 0; page_index < pages; ++page_index) {
        const std::string relative_image = "pages/page_" + std::to_string(page_index + 1) + ".png";
        const auto output_path = output_dir / std::filesystem::path(relative_image);
        if (!reader.renderPageToPng(page_index, options.dpi, output_path.string())) {
            std::cerr << "error: failed to render page " << page_index + 1 << '\n';
            return 2;
        }
        manifest["pages"].push_back({
            {"page_index", page_index},
            {"page_number", page_index + 1},
            {"image", relative_image},
        });
        std::cout << "wrote: " << output_path.string() << '\n';
    }

    const auto manifest_path = output_dir / "document.json";
    std::ofstream manifest_file(manifest_path);
    if (!manifest_file) {
        std::cerr << "error: failed to write manifest: " << manifest_path << '\n';
        return 2;
    }
    manifest_file << manifest.dump(2) << '\n';
    std::cout << "wrote: " << manifest_path.string() << '\n';
#else
    std::cout << "input_pdf: " << options.input_pdf << '\n'
              << "output_dir: " << options.output_dir << '\n'
              << "dpi: " << options.dpi << '\n'
              << "debug: " << (options.debug ? "true" : "false") << '\n';
#endif

    return 0;
}
