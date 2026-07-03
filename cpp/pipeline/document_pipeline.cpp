#include "pipeline/document_pipeline.h"

#include "document/parsed_document.h"
#include "export/document_exporter.h"
#include "pipeline/document_backend_factory.h"
#include "pipeline/pipeline_context.h"
#include "pipeline/stage_interfaces.h"

#if DOC_PARSER_ENABLE_OPENCV
#include "image/image_preprocessor.h"
#endif

#include <iostream>
#include <string>
#include <vector>

namespace doc_parser::pipeline {
namespace {

#if DOC_PARSER_ENABLE_OPENCV
std::string relativeToOutputRoot(const std::filesystem::path& path, const PipelineContext& context) {
    const std::filesystem::path relative_path = path.lexically_relative(context.output.root);
    if (relative_path.empty()) {
        return path.filename().generic_string();
    }
    return relative_path.generic_string();
}
#endif

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
#else
    (void)pages;
#endif

    return true;
}

bool assembleParsedDocument(const IDocumentBackend& backend,
                            const PipelineContext& context,
                            const std::vector<document::PageArtifact>& pages,
                            const std::vector<document::PageText>& page_texts,
                            document::ParsedDocument& document) {
    if (pages.size() != page_texts.size()) {
        std::cerr << "error: page artifact count does not match text page count\n";
        return false;
    }

    document = {};
    document.source.path = backend.sourcePath();
    document.source.type = backend.sourceType();
    document.dpi = context.render.dpi;
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

} // namespace

bool DocumentPipeline::run(const app::CliOptions& options) const {
    const PipelineContext context = PipelineContext::fromOptions(options);

    const auto backend = createDefaultDocumentBackend();
    if (backend == nullptr) {
        std::cerr << "error: no document backend is enabled\n";
        return false;
    }

    if (!backend->open(context.input_pdf)) {
        std::cerr << "error: failed to open PDF: " << context.input_pdf << '\n';
        return false;
    }

    std::cout << "input_pdf: " << backend->sourcePath() << '\n'
              << "output_dir: " << context.output.root.string() << '\n'
              << "dpi: " << context.render.dpi << '\n'
              << "debug: " << (context.debug ? "true" : "false") << '\n'
              << "pages: " << backend->pageCount() << '\n';

    std::vector<document::PageArtifact> rendered_pages;
    if (!backend->renderPages(context, rendered_pages)) {
        return false;
    }

    for (const auto& page : rendered_pages) {
        std::cout << "wrote: " << page.output_path.string() << '\n';
    }

    if (!preprocessDebugImages(context, rendered_pages)) {
        return false;
    }

    std::vector<document::PageText> page_texts;
    if (!backend->extractText(context, rendered_pages, page_texts)) {
        return false;
    }

    document::ParsedDocument parsed_document;
    if (!assembleParsedDocument(*backend, context, rendered_pages, page_texts, parsed_document)) {
        return false;
    }

    const auto document_exporter = exporter::createDefaultDocumentExporter();
    if (document_exporter == nullptr) {
        std::cerr << "error: no document exporter is enabled\n";
        return false;
    }

    if (!document_exporter->write({
            context.debug,
            context.output.manifest_json,
            &parsed_document,
        })) {
        return false;
    }

    std::cout << "wrote: " << context.output.manifest_json.string() << '\n';
    return true;
}

} // namespace doc_parser::pipeline
