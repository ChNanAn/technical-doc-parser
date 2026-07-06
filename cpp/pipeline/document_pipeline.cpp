#include "pipeline/document_pipeline.h"

#include "assembly/document_assembler.h"
#include "document/parsed_document.h"
#include "export/document_exporter.h"
#include "layout/layout_service.h"
#include "ocr/ocr_service.h"
#include "pipeline/backend_registry.h"
#include "pipeline/document_backend_factory.h"
#include "pipeline/layout_analysis_stage.h"
#include "pipeline/pipeline_context.h"
#include "pipeline/pipeline_trace.h"
#include "pipeline/stage_interfaces.h"
#include "pipeline/table_recognition_stage.h"
#include "pipeline/text_extraction_stage.h"
#include "table/table_service.h"

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

} // namespace

bool DocumentPipeline::run(const app::CliOptions& options) const {
    const PipelineContext context = PipelineContext::fromOptions(options);
    PipelineTrace trace;
    const auto trace_path = context.output.debug_dir / "pipeline_trace.json";

    const auto write_trace = [&]() {
        if (!context.debug) {
            return true;
        }

        const common::Status status = trace.write(trace_path);
        if (!status.okStatus()) {
            std::cerr << "error: " << status.message() << '\n';
            return false;
        }
        return true;
    };

    const auto fail = [&](const std::string& stage, const std::string& message) {
        trace.record(stage, "failed", message);
        (void)write_trace();
        return false;
    };

    auto backend = createDocumentBackend(context.backends.document);
    if (backend.source == nullptr) {
        std::cerr << "error: no document backend is enabled\n";
        return fail("backend", "no matching document backend is enabled");
    }

    if (!backend.source->open(context.input_pdf)) {
        std::cerr << "error: failed to open PDF: " << context.input_pdf << '\n';
        return fail("open_document", "failed to open input document");
    }
    trace.record("open_document", "succeeded", backend.source->sourcePath());

    std::cout << "input_pdf: " << backend.source->sourcePath() << '\n'
              << "output_dir: " << context.output.root.string() << '\n'
              << "dpi: " << context.render.dpi << '\n'
              << "debug: " << (context.debug ? "true" : "false") << '\n'
              << "pages: " << backend.source->pageCount() << '\n';

    if (backend.renderer == nullptr) {
        std::cerr << "error: document backend cannot render pages\n";
        return fail("render_pages", "document backend cannot render pages");
    }

    std::vector<document::PageArtifact> rendered_pages;
    if (!backend.renderer->renderPages(context, rendered_pages)) {
        return fail("render_pages", "failed to render page artifacts");
    }
    trace.record("render_pages", "succeeded", std::to_string(rendered_pages.size()) + " pages");

    for (const auto& page : rendered_pages) {
        std::cout << "wrote: " << page.output_path.string() << '\n';
    }

    if (!preprocessDebugImages(context, rendered_pages)) {
        return fail("preprocess_debug_images", "failed to write debug preprocessing images");
    }
    trace.record("preprocess_debug_images", "succeeded");

    BackendSelectionResult backend_selection = createPipelineServices(context.backends);
    if (!backend_selection.ok) {
        return fail(backend_selection.error_stage, backend_selection.error_message);
    }
    trace.record("configure_backends", "succeeded", backend_selection.trace_message);

    const TextExtractionStage text_extraction(backend.native_text_extractor, *backend_selection.services.ocr);
    std::vector<document::PageText> page_texts;
    common::Status stage_status = text_extraction.extract(context, rendered_pages, page_texts);
    if (!stage_status.okStatus()) {
        return fail("text_extraction", stage_status.message());
    }
    trace.record("text_extraction", "succeeded", std::to_string(page_texts.size()) + " pages");

    const LayoutAnalysisStage layout_analysis(*backend_selection.services.layout);
    std::vector<document::PageLayout> page_layouts;
    stage_status = layout_analysis.analyze(context, rendered_pages, page_texts, page_layouts);
    if (!stage_status.okStatus()) {
        return fail("layout_analysis", stage_status.message());
    }
    trace.record("layout_analysis", "succeeded", std::to_string(page_layouts.size()) + " pages");

    const TableRecognitionStage table_recognition(*backend_selection.services.table);
    std::vector<document::PageTables> page_tables;
    stage_status = table_recognition.recognize(context, rendered_pages, page_texts, page_layouts, page_tables);
    if (!stage_status.okStatus()) {
        return fail("table_recognition", stage_status.message());
    }
    trace.record("table_recognition", "succeeded", std::to_string(page_tables.size()) + " pages");

    document::ParsedDocument parsed_document;
    document::PipelineArtifacts artifacts;
    const assembly::DocumentAssembler document_assembler;
    if (!document_assembler.assemble(
            {
                backend.source->sourcePath(),
                backend.source->sourceType(),
                context.render.dpi,
                rendered_pages,
                page_texts,
                page_layouts,
                page_tables,
            },
            parsed_document,
            artifacts)) {
        std::cerr << "error: failed to assemble document\n";
        return fail("document_assembly", "failed to assemble document");
    }
    trace.record("document_assembly", "succeeded", std::to_string(parsed_document.blocks.size()) + " blocks");

    const auto document_exporter = exporter::createDefaultDocumentExporter();
    if (document_exporter == nullptr) {
        std::cerr << "error: no document exporter is enabled\n";
        return fail("export", "no document exporter is enabled");
    }

    if (!document_exporter->write({
            context.debug,
            context.output.manifest_json,
            &parsed_document,
            &artifacts,
        })) {
        return fail("export", "failed to write document manifest");
    }
    trace.record("export", "succeeded", context.output.manifest_json.string());

    std::cout << "wrote: " << context.output.manifest_json.string() << '\n';
    return write_trace();
}

} // namespace doc_parser::pipeline
