#include "pipeline/document_pipeline.h"

#include "pipeline/pipeline_context.h"

#if DOC_PARSER_ENABLE_PDFIUM
#include "pdf/pdf_library.h"
#include "pdf/pdf_page_renderer.h"
#include "pdf/pdf_reader.h"
#endif

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>

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

    pdf::PdfPageRenderer page_renderer;
    std::vector<pdf::RenderedPage> rendered_pages;
    if (!page_renderer.renderPages(
            reader,
            {
                context.render.dpi,
                context.output.root,
                context.output.pages_dir,
            },
            rendered_pages
        )) {
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

    for (const auto& page : rendered_pages) {
        manifest["pages"].push_back({
            {"page_index", page.page_index},
            {"page_number", page.page_number},
            {"image", page.relative_image},
        });
        std::cout << "wrote: " << page.output_path.string() << '\n';
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
