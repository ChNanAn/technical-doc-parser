#include "pipeline/document_pipeline.h"

#include "pipeline/pipeline_context.h"

#if DOC_PARSER_ENABLE_PDFIUM
#include "pdf/pdf_library.h"
#include "pdf/pdf_reader.h"
#endif

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace doc_parser::pipeline {

bool DocumentPipeline::run(const app::CliOptions& options) const {
    const PipelineContext context = PipelineContext::fromOptions(options);

#if DOC_PARSER_ENABLE_PDFIUM
    pdf::PdfLibrary pdf_library;
    pdf::PdfReader reader;

    if (!reader.open(context.input_pdf.string())) {
        std::cerr << "error: failed to open PDF: " << context.input_pdf << '\n';
        return false;
    }

    const int pages = reader.pageCount();
    std::cout << "input_pdf: " << context.input_pdf.string() << '\n'
              << "output_dir: " << context.output.root.string() << '\n'
              << "dpi: " << context.render.dpi << '\n'
              << "debug: " << (context.debug ? "true" : "false") << '\n'
              << "pages: " << pages << '\n';

    std::error_code ec;
    std::filesystem::create_directories(context.output.pages_dir, ec);
    if (ec) {
        std::cerr << "error: failed to create output directory: " << context.output.pages_dir << '\n';
        return false;
    }

    nlohmann::json manifest;
    manifest["source"] = {
        {"path", context.input_pdf.string()},
        {"type", "pdf"},
    };
    manifest["render"] = {
        {"dpi", context.render.dpi},
    };
    manifest["pages"] = nlohmann::json::array();

    for (int page_index = 0; page_index < pages; ++page_index) {
        const std::string relative_image = "pages/page_" + std::to_string(page_index + 1) + ".png";
        const auto output_path = context.output.root / std::filesystem::path(relative_image);
        if (!reader.renderPageToPng(page_index, context.render.dpi, output_path.string())) {
            std::cerr << "error: failed to render page " << page_index + 1 << '\n';
            return false;
        }
        manifest["pages"].push_back({
            {"page_index", page_index},
            {"page_number", page_index + 1},
            {"image", relative_image},
        });
        std::cout << "wrote: " << output_path.string() << '\n';
    }

    std::ofstream manifest_file(context.output.manifest_json);
    if (!manifest_file) {
        std::cerr << "error: failed to write manifest: " << context.output.manifest_json << '\n';
        return false;
    }
    manifest_file << manifest.dump(2) << '\n';
    std::cout << "wrote: " << context.output.manifest_json.string() << '\n';
    return true;
#else
    std::cout << "input_pdf: " << context.input_pdf.string() << '\n'
              << "output_dir: " << context.output.root.string() << '\n'
              << "dpi: " << context.render.dpi << '\n'
              << "debug: " << (context.debug ? "true" : "false") << '\n';
    return true;
#endif
}

}  // namespace doc_parser::pipeline
