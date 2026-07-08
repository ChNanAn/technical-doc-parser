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
#include "pipeline/reading_order_stage.h"
#include "pipeline/stage_interfaces.h"
#include "pipeline/table_recognition_stage.h"
#include "pipeline/text_extraction_stage.h"
#include "table/table_service.h"

#if DOC_PARSER_ENABLE_OPENCV
#include "image/image_preprocessor.h"
#endif

#include <spdlog/spdlog.h>
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
            spdlog::error("failed to preprocess image for page {}", page.page_number);
            return false;
        }

        page.debug_images.push_back({
            "preprocessed",
            relativeToOutputRoot(output_path, context),
            output_path,
        });
        spdlog::info("wrote: {}", output_path.string());
    }
#else
    (void)pages;
#endif

    return true;
}

} // namespace

bool DocumentPipeline::run(const app::CliOptions& options) const {
    const PipelineContext context = PipelineContext::fromOptions(options);

    const auto fail = [&](const std::string& stage, const std::string& message) {
        spdlog::error("{}: {}", stage, message);
        return false;
    };

    auto backend = createDocumentBackend(context.backends.document);
    if (backend.source == nullptr) {
        return fail("backend", "no matching document backend is enabled");
    }

    if (!backend.source->open(context.input_pdf)) {
        return fail("open_document", "failed to open input document: " + context.input_pdf.string());
    }

    spdlog::info("input: {}", backend.source->sourcePath());
    spdlog::info("output_dir: {}", context.output.root.string());
    spdlog::info("dpi: {}", context.render.dpi);
    spdlog::info("debug: {}", context.debug);
    spdlog::info("pages: {}", backend.source->pageCount());

    if (backend.renderer == nullptr) {
        return fail("render_pages", "document backend cannot render pages");
    }

    std::vector<document::PageArtifact> rendered_pages;
    if (!backend.renderer->renderPages(context, rendered_pages)) {
        return fail("render_pages", "failed to render page artifacts");
    }
    spdlog::info("rendered pages: {}", rendered_pages.size());

    for (const auto& page : rendered_pages) {
        spdlog::info("wrote: {}", page.output_path.string());
    }

    if (!preprocessDebugImages(context, rendered_pages)) {
        return fail("preprocess_debug_images", "failed to write debug preprocessing images");
    }
    spdlog::debug("preprocessed debug images");

    BackendSelectionResult backend_selection = createPipelineServices(context.backends);
    if (!backend_selection.ok) {
        return fail(backend_selection.error_stage, backend_selection.error_message);
    }
    spdlog::info("configured backends: {}", backend_selection.trace_message);

    const TextExtractionStage text_extraction(backend.native_text_extractor, *backend_selection.services.ocr);
    std::vector<document::PageText> page_texts;
    common::Status stage_status = text_extraction.extract(context, rendered_pages, page_texts);
    if (!stage_status.okStatus()) {
        return fail("text_extraction", stage_status.message());
    }
    spdlog::info("extracted text pages: {}", page_texts.size());

    const LayoutAnalysisStage layout_analysis(*backend_selection.services.layout);
    std::vector<document::PageLayout> page_layouts;
    stage_status = layout_analysis.analyze(context, rendered_pages, page_texts, page_layouts);
    if (!stage_status.okStatus()) {
        return fail("layout_analysis", stage_status.message());
    }
    spdlog::info("analyzed layout pages: {}", page_layouts.size());

    const ReadingOrderStage reading_order(*backend_selection.services.reading_order);
    std::vector<document::PageReadingOrder> page_reading_orders;
    stage_status = reading_order.order(context, rendered_pages, page_layouts, page_reading_orders);
    if (!stage_status.okStatus()) {
        return fail("reading_order", stage_status.message());
    }
    spdlog::info("computed reading order pages: {}", page_reading_orders.size());

    const TableRecognitionStage table_recognition(*backend_selection.services.table);
    std::vector<document::PageTables> page_tables;
    stage_status = table_recognition.recognize(context, rendered_pages, page_texts, page_layouts, page_tables);
    if (!stage_status.okStatus()) {
        return fail("table_recognition", stage_status.message());
    }
    spdlog::info("recognized table pages: {}", page_tables.size());

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
                page_reading_orders,
                page_tables,
            },
            parsed_document,
            artifacts)) {
        return fail("document_assembly", "failed to assemble document");
    }
    spdlog::info("assembled document blocks: {}", parsed_document.blocks.size());

    const auto document_exporter = exporter::createDefaultDocumentExporter();
    if (document_exporter == nullptr) {
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

    spdlog::info("wrote: {}", context.output.manifest_json.string());
    return true;
}

} // namespace doc_parser::pipeline
