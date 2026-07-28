#include "pipeline/document_pipeline.h"

#include "common/file_fingerprint.h"

#include "assembly/document_assembler.h"
#include "document/parsed_document.h"
#include "document/warning_aggregator.h"
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
#include <utility>
#include <vector>

namespace doc_parser::pipeline {
namespace {

using Clock = std::chrono::steady_clock;

long long elapsedMilliseconds(const Clock::time_point& started) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
}

common::Status stageFailed(IStageObserver& observer,
                           const std::string& stage,
                           const std::string& code,
                           const std::string& message,
                           bool retryable = false) {
    observer.onStageFailed({stage, code, message, retryable});
    return common::Status::error(code, message, stage, retryable);
}

common::Status deadlineStatus(const PipelineRunOptions& options,
                              const Clock::time_point& run_started,
                              IStageObserver& observer,
                              const std::string& next_stage) {
    if (options.timeout_seconds <= 0 || Clock::now() - run_started < std::chrono::seconds(options.timeout_seconds)) {
        return common::Status::ok();
    }
    return stageFailed(observer,
                       next_stage,
                       "run_timeout",
                       "pipeline exceeded its " + std::to_string(options.timeout_seconds) + " second deadline");
}

std::string configuredServicesTrace(const RunProvenance& provenance) {
    const BackendResolution& backends = provenance.backends;
    return "registry=" + backends.config_source + ", document=" + backends.resolved.document +
           ", ocr=" + backends.resolved.ocr + ", layout=" + backends.resolved.layout +
           ", table=" + backends.resolved.table;
}

const std::string& resolvedBackend(const std::string& resolved, const std::string& requested) {
    return resolved.empty() ? requested : resolved;
}

void recordDiagnostics(const std::vector<common::Diagnostic>& diagnostics,
                       std::vector<common::Diagnostic>& run_diagnostics,
                       RunProvenance& provenance,
                       IStageObserver& observer) {
    for (const common::Diagnostic& diagnostic : diagnostics) {
        run_diagnostics.push_back(diagnostic);
        observer.onStageWarning(diagnostic);
        const auto failed = diagnostic.details.find("failed_backend");
        const auto fallback = diagnostic.details.find("fallback_backend");
        if (failed != diagnostic.details.end() && fallback != diagnostic.details.end()) {
            const auto reason = diagnostic.details.find("reason");
            provenance.fallbacks.push_back({
                diagnostic.stage,
                diagnostic.page_number,
                failed->second,
                fallback->second,
                reason == diagnostic.details.end() ? diagnostic.message : reason->second,
            });
        }
    }
}

void applyRunMetadata(const PipelineRunOptions& options,
                      const std::vector<common::Diagnostic>& diagnostics,
                      const RunProvenance& provenance,
                      document::ParsedDocument& document) {
    document.producer.name = provenance.engine_name;
    document.producer.version = provenance.engine_version;
    document.producer.git_revision = provenance.git_revision;
    document.producer.run_id = options.run_id;
    std::vector<document::DocumentWarning> warnings;
    warnings.reserve(diagnostics.size());
    for (const common::Diagnostic& diagnostic : diagnostics) {
        warnings.push_back({
            diagnostic.code,
            diagnostic.message,
            diagnostic.stage,
            diagnostic.page_number > 0 ? "page_" + std::to_string(diagnostic.page_number) : std::string{},
            {},
            diagnostic.details,
        });
    }
    document.warnings = document::aggregateWarnings(warnings);
    if (!document.warnings.empty()) {
        document.status = document::DocumentStatus::Partial;
    }
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

common::Status DocumentPipeline::run(const PipelineRunOptions& options,
                                     const BackendRegistry& registry,
                                     IStageObserver& observer) const {
    const Clock::time_point run_started = Clock::now();
    document::ParsedDocument document;
    document::PipelineArtifacts artifacts;
    RunProvenance provenance;
    common::Status status =
        parseInternal(options, nullptr, nullptr, document, artifacts, provenance, observer, &registry);
    if (!status.okStatus()) {
        return status;
    }
    if (const common::Status deadline = deadlineStatus(options, run_started, observer, "export");
        !deadline.okStatus()) {
        return deadline;
    }
    return exportResult(options, document, artifacts, observer);
}

common::Status DocumentPipeline::parse(const PipelineRunOptions& options,
                                       PipelineServices& services,
                                       const RunProvenance& service_provenance,
                                       document::ParsedDocument& document,
                                       document::PipelineArtifacts& artifacts,
                                       RunProvenance& run_provenance,
                                       IStageObserver& observer) const {
    return parseInternal(
        options, &services, &service_provenance, document, artifacts, run_provenance, observer, nullptr);
}

common::Status DocumentPipeline::parseInternal(const PipelineRunOptions& options,
                                               PipelineServices* services,
                                               const RunProvenance* service_provenance,
                                               document::ParsedDocument& parsed_document,
                                               document::PipelineArtifacts& artifacts,
                                               RunProvenance& run_provenance,
                                               IStageObserver& observer,
                                               const BackendRegistry* registry) const {
    const Clock::time_point run_started = Clock::now();
    const PipelineContext context = PipelineContext::fromOptions(options);

    Clock::time_point stage_started = Clock::now();
    observer.onStageStarted({"configure", "registry", 1});
    PipelineServiceCreationResult service_creation;
    if (services == nullptr) {
        if (registry == nullptr) {
            return stageFailed(observer,
                               "configure",
                               "configure.backend_registry_required",
                               "pipeline service creation requires an explicit backend registry");
        }
        service_creation = createPipelineServices(context.backends, *registry);
        if (!service_creation.status.okStatus()) {
            observer.onStageFailed({service_creation.status.stage(),
                                    service_creation.status.code(),
                                    service_creation.status.message(),
                                    service_creation.status.retryable()});
            return service_creation.status;
        }
        services = &service_creation.services;
    }
    run_provenance = service_provenance == nullptr ? service_creation.provenance : *service_provenance;
    run_provenance.run_id = options.run_id;
    observer.onStageProgress({"configure", 1, 1});
    observer.onStageCompleted({"configure", elapsedMilliseconds(stage_started)});
    observer.onRunConfigured(run_provenance);
    spdlog::info("configured services: {}", configuredServicesTrace(run_provenance));

    auto& document = services->document;
    std::vector<common::Diagnostic> run_diagnostics;

    stage_started = Clock::now();
    observer.onStageStarted(
        {"open", resolvedBackend(run_provenance.backends.resolved.document, context.backends.document), 1});
    if (!document.source->open(context.input_path)) {
        spdlog::error("open_document: failed to open input document: {}", context.input_path.string());
        return stageFailed(observer, "open", "open_document_failed", "failed to open input document");
    }
    common::FileFingerprint source_fingerprint;
    const common::Status fingerprint_status =
        common::fingerprintFile(document.source->sourcePath(), source_fingerprint);
    if (!fingerprint_status.okStatus()) {
        spdlog::error("source_fingerprint: code={} path={} reason={}",
                      fingerprint_status.code(),
                      document.source->sourcePath(),
                      fingerprint_status.message());
        return stageFailed(
            observer, "open", fingerprint_status.code(), fingerprint_status.message(), fingerprint_status.retryable());
    }
    spdlog::debug(
        "source_fingerprint: size_bytes={} sha256={}", source_fingerprint.size_bytes, source_fingerprint.sha256);
    if (options.maximum_pages > 0 && document.source->pageCount() > options.maximum_pages) {
        return stageFailed(observer,
                           "open",
                           "maximum_pages_exceeded",
                           "document has " + std::to_string(document.source->pageCount()) + " pages; limit is " +
                               std::to_string(options.maximum_pages));
    }
    observer.onStageProgress({"open", 1, 1});
    observer.onStageCompleted({"open", elapsedMilliseconds(stage_started)});

    spdlog::info("input: {}", document.source->sourcePath());
    spdlog::info("output_dir: {}", context.output.root.string());
    spdlog::info("dpi: {}", context.render.dpi);
    spdlog::info("debug: {}", context.debug);
    spdlog::info("pages: {}", document.source->pageCount());

    if (const common::Status deadline = deadlineStatus(options, run_started, observer, "render");
        !deadline.okStatus()) {
        return deadline;
    }
    if (document.renderer == nullptr) {
        spdlog::error("render_pages: document source cannot render pages");
        return stageFailed(observer, "render", "renderer_unavailable", "document source cannot render pages");
    }

    stage_started = Clock::now();
    observer.onStageStarted({"render",
                             resolvedBackend(run_provenance.backends.resolved.document, context.backends.document),
                             document.source->pageCount()});
    std::vector<document::PageArtifact> rendered_pages;
    if (!document.renderer->renderPages({context.render.dpi, context.output.root, context.output.pages_dir},
                                        rendered_pages)) {
        spdlog::error("render_pages: failed to render page artifacts");
        return stageFailed(observer, "render", "render_failed", "failed to render page artifacts", true);
    }
    spdlog::info("rendered pages: {}", rendered_pages.size());

    for (const auto& page : rendered_pages) {
        spdlog::info("wrote: {}", page.output_path.string());
        observer.onArtifactReady({"render", "page_image", page.output_path, page.page_number});
        observer.onStageProgress({"render", page.page_number, static_cast<int>(rendered_pages.size())});
    }

    if (!preprocessDebugImages(context, rendered_pages)) {
        spdlog::error("preprocess_debug_images: failed to write debug preprocessing images");
        return stageFailed(observer, "render", "preprocess_failed", "failed to write debug preprocessing images");
    }
    observer.onStageCompleted({"render", elapsedMilliseconds(stage_started)});
    spdlog::debug("preprocessed debug images");

    if (const common::Status deadline = deadlineStatus(options, run_started, observer, "text"); !deadline.okStatus()) {
        return deadline;
    }
    stage_started = Clock::now();
    observer.onStageStarted({"text",
                             resolvedBackend(run_provenance.backends.resolved.ocr, context.backends.ocr),
                             static_cast<int>(rendered_pages.size())});
    const TextExtractionStage text_extraction(document.native_text_extractor, *services->ocr);
    StageResult<std::vector<document::PageText>> text_result = text_extraction.extract(context, rendered_pages);
    if (!text_result.ok()) {
        spdlog::error("text_extraction: {}", text_result.status.message());
        return stageFailed(
            observer, "text", text_result.status.code(), text_result.status.message(), text_result.status.retryable());
    }
    recordDiagnostics(text_result.diagnostics, run_diagnostics, run_provenance, observer);
    std::vector<document::PageText> page_texts = std::move(text_result.value);
    observer.onStageProgress({"text", static_cast<int>(page_texts.size()), static_cast<int>(rendered_pages.size())});
    observer.onStageCompleted({"text", elapsedMilliseconds(stage_started)});
    spdlog::info("extracted text pages: {}", page_texts.size());

    if (const common::Status deadline = deadlineStatus(options, run_started, observer, "layout");
        !deadline.okStatus()) {
        return deadline;
    }
    stage_started = Clock::now();
    observer.onStageStarted({"layout",
                             resolvedBackend(run_provenance.backends.resolved.layout, context.backends.layout),
                             static_cast<int>(rendered_pages.size())});
    const LayoutAnalysisStage layout_analysis(*services->layout);
    StageResult<std::vector<document::PageLayout>> layout_result =
        layout_analysis.analyze(context, rendered_pages, page_texts);
    if (!layout_result.ok()) {
        spdlog::error("layout_analysis: {}", layout_result.status.message());
        return stageFailed(observer,
                           "layout",
                           layout_result.status.code(),
                           layout_result.status.message(),
                           layout_result.status.retryable());
    }
    recordDiagnostics(layout_result.diagnostics, run_diagnostics, run_provenance, observer);
    std::vector<document::PageLayout> page_layouts = std::move(layout_result.value);
    observer.onStageProgress(
        {"layout", static_cast<int>(page_layouts.size()), static_cast<int>(rendered_pages.size())});
    observer.onStageCompleted({"layout", elapsedMilliseconds(stage_started)});
    spdlog::info("analyzed layout pages: {}", page_layouts.size());

    if (const common::Status deadline = deadlineStatus(options, run_started, observer, "table"); !deadline.okStatus()) {
        return deadline;
    }
    stage_started = Clock::now();
    observer.onStageStarted({"table",
                             resolvedBackend(run_provenance.backends.resolved.table, context.backends.table),
                             static_cast<int>(rendered_pages.size())});
    const TableRecognitionStage table_recognition(*services->table);
    StageResult<std::vector<document::PageTables>> table_result =
        table_recognition.recognize(context, rendered_pages, page_texts, page_layouts);
    if (!table_result.ok()) {
        spdlog::error("table_recognition: {}", table_result.status.message());
        return stageFailed(observer,
                           "table",
                           table_result.status.code(),
                           table_result.status.message(),
                           table_result.status.retryable());
    }
    recordDiagnostics(table_result.diagnostics, run_diagnostics, run_provenance, observer);
    std::vector<document::PageTables> page_tables = std::move(table_result.value);
    observer.onStageProgress({"table", static_cast<int>(page_tables.size()), static_cast<int>(rendered_pages.size())});
    observer.onStageCompleted({"table", elapsedMilliseconds(stage_started)});
    spdlog::info("recognized table pages: {}", page_tables.size());

    if (const common::Status deadline = deadlineStatus(options, run_started, observer, "reading_order");
        !deadline.okStatus()) {
        return deadline;
    }
    stage_started = Clock::now();
    observer.onStageStarted({"reading_order", "docling-like", static_cast<int>(rendered_pages.size())});
    const ReadingOrderStage reading_order(*services->reading_order);
    StageResult<std::vector<document::PageReadingOrder>> reading_order_result =
        reading_order.order(context, rendered_pages, page_layouts);
    if (!reading_order_result.ok()) {
        spdlog::error("reading_order: {}", reading_order_result.status.message());
        return stageFailed(observer,
                           "reading_order",
                           reading_order_result.status.code(),
                           reading_order_result.status.message(),
                           reading_order_result.status.retryable());
    }
    recordDiagnostics(reading_order_result.diagnostics, run_diagnostics, run_provenance, observer);
    std::vector<document::PageReadingOrder> page_reading_orders = std::move(reading_order_result.value);
    observer.onStageProgress(
        {"reading_order", static_cast<int>(page_reading_orders.size()), static_cast<int>(rendered_pages.size())});
    observer.onStageCompleted({"reading_order", elapsedMilliseconds(stage_started)});
    spdlog::info("computed reading order pages: {}", page_reading_orders.size());

    if (const common::Status deadline = deadlineStatus(options, run_started, observer, "assembly");
        !deadline.okStatus()) {
        return deadline;
    }
    stage_started = Clock::now();
    observer.onStageStarted({"assembly", "document-assembler", 1});
    const assembly::DocumentAssembler document_assembler;
    assembly::DocumentAssembleRequest assemble_request{
        document.source->sourcePath(),
        document.source->sourceType(),
        context.render.dpi,
        rendered_pages,
        page_texts,
        page_layouts,
        page_reading_orders,
        page_tables,
    };
    assemble_request.source_size_bytes = source_fingerprint.size_bytes;
    assemble_request.source_sha256 = source_fingerprint.sha256;
    if (!document_assembler.assemble(assemble_request, parsed_document, artifacts)) {
        spdlog::error("document_assembly: failed to assemble document");
        return stageFailed(observer, "assembly", "assembly_failed", "failed to assemble document");
    }
    applyRunMetadata(options, run_diagnostics, run_provenance, parsed_document);
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
    return common::Status::ok();
}

common::Status DocumentPipeline::exportResult(const PipelineRunOptions& options,
                                              const document::ParsedDocument& document,
                                              const document::PipelineArtifacts& artifacts,
                                              IStageObserver& observer) const {
    const PipelineContext context = PipelineContext::fromOptions(options);
    const Clock::time_point stage_started = Clock::now();
    observer.onStageStarted({"export", "multi-format", 3});
    const auto document_exporter = exporter::createDefaultDocumentExporter();
    if (document_exporter == nullptr) {
        spdlog::error("export: no document exporter is enabled");
        return stageFailed(observer, "export", "exporter_unavailable", "no document exporter is enabled");
    }

    const common::Status export_status = document_exporter->write({
        context.debug,
        context.output.manifest_json,
        &document,
        &artifacts,
    });
    if (!export_status.okStatus()) {
        spdlog::error("export failed [{}]: {}", export_status.code(), export_status.message());
        return stageFailed(observer,
                           export_status.stage().empty() ? "export" : export_status.stage(),
                           export_status.code(),
                           export_status.message(),
                           export_status.retryable());
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
    return common::Status::ok();
}

} // namespace doc_parser::pipeline
