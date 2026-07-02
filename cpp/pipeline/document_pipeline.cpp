#include "pipeline/document_pipeline.h"

#include "pipeline/pipeline_context.h"

#if DOC_PARSER_ENABLE_PDFIUM
#include "export/json_manifest_writer.h"
#include "pdf/pdf_library.h"
#include "pdf/pdf_reader.h"
#include "pdf/render_service.h"
#include "pipeline/text_extraction_stage.h"
#endif

#include <iostream>
#include <vector>

namespace doc_parser::pipeline {

bool DocumentPipeline::run(const app::CliOptions& options) const {
    const PipelineContext context = PipelineContext::fromOptions(options);

#if DOC_PARSER_ENABLE_PDFIUM
    const std::string input_pdf_path = context.input_pdf.string();

    pdf::PdfLibrary library; // PDFium process init
    pdf::PdfReader source;
    if (!source.open(input_pdf_path)) {
        std::cerr << "error: failed to open PDF: " << context.input_pdf << '\n';
        return false;
    }

    std::cout << "input_pdf: " << input_pdf_path << '\n'
              << "output_dir: " << context.output.root.string() << '\n'
              << "dpi: " << context.render.dpi << '\n'
              << "debug: " << (context.debug ? "true" : "false") << '\n'
              << "pages: " << source.pageCount() << '\n';

    TextExtractionStage text_extraction;
    std::vector<document::PageText> page_texts;
    if (!text_extraction.extract(source, context.render.dpi, page_texts)) {
        return false;
    }

    pdf::RenderService render;
    std::vector<pdf::RenderedPage> rendered_pages;
    if (!render.renderPages(source,
                            {
                                context.render.dpi,
                                context.output.root,
                                context.output.pages_dir,
                            },
                            rendered_pages)) {
        return false;
    }

    for (const auto& page : rendered_pages) {
        std::cout << "wrote: " << page.output_path.string() << '\n';
    }

    const exporter::JsonManifestWriter manifest_writer;
    if (!manifest_writer.write({
            input_pdf_path,
            context.render.dpi,
            context.debug,
            context.output.manifest_json,
            &rendered_pages,
            &page_texts,
        })) {
        return false;
    }

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

} // namespace doc_parser::pipeline
