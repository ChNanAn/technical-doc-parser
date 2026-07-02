#include "pipeline/document_pipeline.h"

#include "document/parsed_document.h"
#include "pipeline/pipeline_context.h"

#if DOC_PARSER_ENABLE_PDFIUM
#include "export/json_manifest_writer.h"
#include "pdf/pdf_document.h"
#include "pdf/render_service.h"
#include "pipeline/text_extraction_stage.h"
#endif

#if DOC_PARSER_ENABLE_OPENCV
#include "image/image_preprocessor.h"
#endif

#include <iostream>
#include <string>
#include <vector>

namespace doc_parser::pipeline {
namespace {

#if DOC_PARSER_ENABLE_PDFIUM
std::string relativeToOutputRoot(const std::filesystem::path& path, const PipelineContext& context) {
    const std::filesystem::path relative_path = path.lexically_relative(context.output.root);
    if (relative_path.empty()) {
        return path.filename().generic_string();
    }
    return relative_path.generic_string();
}

bool preprocessDebugImages(const PipelineContext& context, std::vector<document::PageArtifact>& pages) {
    if (!context.debug) {
        return true;
    }

#if DOC_PARSER_ENABLE_OPENCV
    const image::ImagePreprocessor preprocessor;
    for (auto& page : pages) {
        const std::filesystem::path output_path =
            context.output.debug_dir / ("page_" + std::to_string(page.page_number) + "_preprocessed.png");
        if (!preprocessor.preprocessFile(page.output_path, output_path)) {
            std::cerr << "error: failed to preprocess image for page " << page.page_number << '\n';
            return false;
        }

        page.debug_images.push_back({
            "preprocessed",
            relativeToOutputRoot(output_path, context),
            output_path,
        });
        std::cout << "wrote: " << output_path.string() << '\n';
    }
#endif

    return true;
}

bool assembleParsedDocument(const std::string& input_pdf_path,
                            int dpi,
                            const std::vector<document::PageArtifact>& pages,
                            const std::vector<document::PageText>& page_texts,
                            document::ParsedDocument& document) {
    if (pages.size() != page_texts.size()) {
        std::cerr << "error: page artifact count does not match text page count\n";
        return false;
    }

    document = {};
    document.source.path = input_pdf_path;
    document.source.type = "pdf";
    document.dpi = dpi;
    document.pages.reserve(pages.size());

    for (std::size_t index = 0; index < pages.size(); ++index) {
        document.pages.push_back({
            pages[index].page_index,
            pages[index].page_number,
            pages[index],
            page_texts[index],
        });
    }

    return true;
}
#endif

} // namespace

bool DocumentPipeline::run(const app::CliOptions& options) const {
    const PipelineContext context = PipelineContext::fromOptions(options);

#if DOC_PARSER_ENABLE_PDFIUM
    const std::string input_pdf_path = context.input_pdf.string();

    pdf::PdfDocument source;
    if (!source.open(input_pdf_path)) {
        std::cerr << "error: failed to open PDF: " << context.input_pdf << '\n';
        return false;
    }

    std::cout << "input_pdf: " << input_pdf_path << '\n'
              << "output_dir: " << context.output.root.string() << '\n'
              << "dpi: " << context.render.dpi << '\n'
              << "debug: " << (context.debug ? "true" : "false") << '\n'
              << "pages: " << source.pageCount() << '\n';

    pdf::RenderService render;
    std::vector<document::PageArtifact> rendered_pages;
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

    if (!preprocessDebugImages(context, rendered_pages)) {
        return false;
    }

    TextExtractionStage text_extraction;
    std::vector<document::PageText> page_texts;
    if (!text_extraction.extract(source, rendered_pages, context.render.dpi, page_texts)) {
        return false;
    }

    document::ParsedDocument parsed_document;
    if (!assembleParsedDocument(input_pdf_path, context.render.dpi, rendered_pages, page_texts, parsed_document)) {
        return false;
    }

    const exporter::JsonManifestWriter manifest_writer;
    if (!manifest_writer.write({
            context.debug,
            context.output.manifest_json,
            &parsed_document,
        })) {
        return false;
    }

    std::cout << "wrote: " << context.output.manifest_json.string() << '\n';
    return true;
#else
    std::cerr << "error: PDFium integration is disabled; PDF parsing is unavailable\n";
    return false;
#endif
}

} // namespace doc_parser::pipeline
