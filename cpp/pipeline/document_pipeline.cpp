#include "pipeline/document_pipeline.h"

#include "assembly/document_assembler.h"
#include "document/parsed_document.h"
#include "export/document_exporter.h"
#include "pipeline/layout_analysis_stage.h"
#include "pipeline/pipeline_context.h"
#include "pipeline/pipeline_service_factory.h"
#include "pipeline/reading_order_stage.h"
#include "pipeline/table_recognition_stage.h"
#include "pipeline/text_extraction_stage.h"

#if DOC_PARSER_ENABLE_OPENCV
#include "image/image_preprocessor.h"
#endif

#include <chrono>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace doc_parser::pipeline {
namespace {

using Clock = std::chrono::steady_clock;

long long elapsedMilliseconds(const Clock::time_point& started) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
}

void stageFailed(IStageObserver& observer,
                 const std::string& stage,
                 const std::string& code,
                 const std::string& message,
                 bool retryable = false) {
    observer.onStageFailed({stage, code, message, retryable});
}

bool deadlineExceeded(const app::CliOptions& options,
                      const Clock::time_point& run_started,
                      IStageObserver& observer,
                      const std::string& next_stage) {
    if (options.timeout_seconds <= 0 || Clock::now() - run_started < std::chrono::seconds(options.timeout_seconds)) {
        return false;
    }
    stageFailed(observer,
                next_stage,
                "run_timeout",
                "pipeline exceeded its " + std::to_string(options.timeout_seconds) + " second deadline");
    return true;
}

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
    NullStageObserver observer;
    return run(options, observer);
}

bool DocumentPipeline::run(const app::CliOptions& options, IStageObserver& observer) const {
    const Clock::time_point run_started = Clock::now();
    const PipelineContext context = PipelineContext::fromOptions(options);

    Clock::time_point stage_started = Clock::now();
    observer.onStageStarted({"configure", "registry", 1});
    PipelineServiceCreationResult service_creation = createPipelineServices(context.backends);
    if (!service_creation.ok) {
        stageFailed(observer, "configure", service_creation.error_stage, service_creation.error_message);
        return false;
    }
    observer.onStageProgress({"configure", 1, 1});
    observer.onStageCompleted({"configure", elapsedMilliseconds(stage_started)});
    spdlog::info("configured services: {}", service_creation.trace_message);

    auto& document = service_creation.services.document;

    stage_started = Clock::now();
    observer.onStageStarted({"open", context.backends.document, 1});
    if (!document.source->open(context.input_pdf)) {
        spdlog::error("open_document: failed to open input document: {}", context.input_pdf.string());
        stageFailed(observer, "open", "open_document_failed", "failed to open input document");
        return false;
    }
    if (options.maximum_pages > 0 && document.source->pageCount() > options.maximum_pages) {
        stageFailed(observer,
                    "open",
                    "maximum_pages_exceeded",
                    "document has " + std::to_string(document.source->pageCount()) + " pages; limit is " +
                        std::to_string(options.maximum_pages));
        return false;
    }
    observer.onStageProgress({"open", 1, 1});
    observer.onStageCompleted({"open", elapsedMilliseconds(stage_started)});

    spdlog::info("input: {}", document.source->sourcePath());
    spdlog::info("output_dir: {}", context.output.root.string());
    spdlog::info("dpi: {}", context.render.dpi);
    spdlog::info("debug: {}", context.debug);
    spdlog::info("pages: {}", document.source->pageCount());

    if (deadlineExceeded(options, run_started, observer, "render")) {
        return false;
    }
    if (document.renderer == nullptr) {
        spdlog::error("render_pages: document source cannot render pages");
        stageFailed(observer, "render", "renderer_unavailable", "document source cannot render pages");
        return false;
    }

    stage_started = Clock::now();
    observer.onStageStarted({"render", context.backends.document, document.source->pageCount()});
    std::vector<document::PageArtifact> rendered_pages;
    if (!document.renderer->renderPages({context.render.dpi, context.output.root, context.output.pages_dir},
                                        rendered_pages)) {
        spdlog::error("render_pages: failed to render page artifacts");
        stageFailed(observer, "render", "render_failed", "failed to render page artifacts", true);
        return false;
    }
    spdlog::info("rendered pages: {}", rendered_pages.size());

    for (const auto& page : rendered_pages) {
        spdlog::info("wrote: {}", page.output_path.string());
        observer.onArtifactReady({"render", "page_image", page.output_path, page.page_number});
        observer.onStageProgress({"render", page.page_number, static_cast<int>(rendered_pages.size())});
    }

    if (!preprocessDebugImages(context, rendered_pages)) {
        spdlog::error("preprocess_debug_images: failed to write debug preprocessing images");
        stageFailed(observer, "render", "preprocess_failed", "failed to write debug preprocessing images");
        return false;
    }
    observer.onStageCompleted({"render", elapsedMilliseconds(stage_started)});
    spdlog::debug("preprocessed debug images");

    if (deadlineExceeded(options, run_started, observer, "text")) {
        return false;
    }
    stage_started = Clock::now();
    observer.onStageStarted({"text", context.backends.ocr, static_cast<int>(rendered_pages.size())});
    const TextExtractionStage text_extraction(document.native_text_extractor, *service_creation.services.ocr);
    std::vector<document::PageText> page_texts;
    common::Status stage_status = text_extraction.extract(context, rendered_pages, page_texts);
    if (!stage_status.okStatus()) {
        spdlog::error("text_extraction: {}", stage_status.message());
        stageFailed(observer, "text", stage_status.code(), stage_status.message());
        return false;
    }
    observer.onStageProgress({"text", static_cast<int>(page_texts.size()), static_cast<int>(rendered_pages.size())});
    observer.onStageCompleted({"text", elapsedMilliseconds(stage_started)});
    spdlog::info("extracted text pages: {}", page_texts.size());

    if (deadlineExceeded(options, run_started, observer, "layout")) {
        return false;
    }
    stage_started = Clock::now();
    observer.onStageStarted({"layout", context.backends.layout, static_cast<int>(rendered_pages.size())});
    const LayoutAnalysisStage layout_analysis(*service_creation.services.layout);
    std::vector<document::PageLayout> page_layouts;
    stage_status = layout_analysis.analyze(context, rendered_pages, page_texts, page_layouts);
    if (!stage_status.okStatus()) {
        spdlog::error("layout_analysis: {}", stage_status.message());
        stageFailed(observer, "layout", stage_status.code(), stage_status.message());
        return false;
    }
    observer.onStageProgress(
        {"layout", static_cast<int>(page_layouts.size()), static_cast<int>(rendered_pages.size())});
    observer.onStageCompleted({"layout", elapsedMilliseconds(stage_started)});
    spdlog::info("analyzed layout pages: {}", page_layouts.size());

    if (deadlineExceeded(options, run_started, observer, "table")) {
        return false;
    }
    stage_started = Clock::now();
    observer.onStageStarted({"table", context.backends.table, static_cast<int>(rendered_pages.size())});
    const TableRecognitionStage table_recognition(*service_creation.services.table);
    std::vector<document::PageTables> page_tables;
    stage_status = table_recognition.recognize(context, rendered_pages, page_texts, page_layouts, page_tables);
    if (!stage_status.okStatus()) {
        spdlog::error("table_recognition: {}", stage_status.message());
        stageFailed(observer, "table", stage_status.code(), stage_status.message());
        return false;
    }
    observer.onStageProgress({"table", static_cast<int>(page_tables.size()), static_cast<int>(rendered_pages.size())});
    observer.onStageCompleted({"table", elapsedMilliseconds(stage_started)});
    spdlog::info("recognized table pages: {}", page_tables.size());

    if (deadlineExceeded(options, run_started, observer, "reading_order")) {
        return false;
    }
    stage_started = Clock::now();
    observer.onStageStarted({"reading_order", "docling-like", static_cast<int>(rendered_pages.size())});
    const ReadingOrderStage reading_order(*service_creation.services.reading_order);
    std::vector<document::PageReadingOrder> page_reading_orders;
    stage_status = reading_order.order(context, rendered_pages, page_layouts, page_reading_orders);
    if (!stage_status.okStatus()) {
        spdlog::error("reading_order: {}", stage_status.message());
        stageFailed(observer, "reading_order", stage_status.code(), stage_status.message());
        return false;
    }
    observer.onStageProgress(
        {"reading_order", static_cast<int>(page_reading_orders.size()), static_cast<int>(rendered_pages.size())});
    observer.onStageCompleted({"reading_order", elapsedMilliseconds(stage_started)});
    spdlog::info("computed reading order pages: {}", page_reading_orders.size());

    if (deadlineExceeded(options, run_started, observer, "assembly")) {
        return false;
    }
    stage_started = Clock::now();
    observer.onStageStarted({"assembly", "document-assembler", 1});
    document::ParsedDocument parsed_document;
    document::PipelineArtifacts artifacts;
    const assembly::DocumentAssembler document_assembler;
    if (!document_assembler.assemble(
            {
                document.source->sourcePath(),
                document.source->sourceType(),
                context.render.dpi,
                rendered_pages,
                page_texts,
                page_layouts,
                page_reading_orders,
                page_tables,
            },
            parsed_document,
            artifacts)) {
        spdlog::error("document_assembly: failed to assemble document");
        stageFailed(observer, "assembly", "assembly_failed", "failed to assemble document");
        return false;
    }
    observer.onStageProgress({"assembly", 1, 1});
    observer.onStageCompleted({"assembly", elapsedMilliseconds(stage_started)});
    std::size_t detected_furniture = 0;
    for (const document::PageLayout& layout : page_layouts) {
        for (const document::LayoutBlock& block : layout.blocks) {
            if (block.type == document::LayoutBlockType::Header || block.type == document::LayoutBlockType::Footer) {
                ++detected_furniture;
            }
        }
    }
    std::size_t emitted_furniture = 0;
    for (const document::DocumentBlock& block : parsed_document.blocks) {
        if (block.type == document::DocumentBlockType::Header || block.type == document::DocumentBlockType::Footer) {
            ++emitted_furniture;
        }
    }
    spdlog::debug("document_assembly: repeated_header_footer_removed={}",
                  detected_furniture >= emitted_furniture ? detected_furniture - emitted_furniture : 0U);
    spdlog::info("assembled document blocks: {}", parsed_document.blocks.size());

    if (deadlineExceeded(options, run_started, observer, "export")) {
        return false;
    }
    stage_started = Clock::now();
    observer.onStageStarted({"export", "multi-format", 3});
    const auto document_exporter = exporter::createDefaultDocumentExporter();
    if (document_exporter == nullptr) {
        spdlog::error("export: no document exporter is enabled");
        stageFailed(observer, "export", "exporter_unavailable", "no document exporter is enabled");
        return false;
    }

    if (!document_exporter->write({
            context.debug,
            context.output.manifest_json,
            &parsed_document,
            &artifacts,
        })) {
        spdlog::error("export: failed to write document manifest");
        stageFailed(observer, "export", "export_failed", "failed to write document output", true);
        return false;
    }

    spdlog::info("wrote: {}", context.output.manifest_json.string());
    std::filesystem::path markdown_path = context.output.manifest_json;
    std::filesystem::path html_path = context.output.manifest_json;
    observer.onArtifactReady({"export", "document_json", context.output.manifest_json, 0});
    observer.onArtifactReady({"export", "document_markdown", markdown_path.replace_extension(".md"), 0});
    observer.onArtifactReady({"export", "document_html", html_path.replace_extension(".html"), 0});
    observer.onStageProgress({"export", 3, 3});
    observer.onStageCompleted({"export", elapsedMilliseconds(stage_started)});
    spdlog::info("wrote: {}", markdown_path.string());
    spdlog::info("wrote: {}", html_path.string());
    return true;
}

} // namespace doc_parser::pipeline
