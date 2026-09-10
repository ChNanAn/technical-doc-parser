#include "pipeline/document_pipeline.h"

#include "common/file_fingerprint.h"

#include "assembly/document_assembler.h"
#include "document/page_rotation.h"
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

#if DOC_PARSER_ENABLE_OPENCV || DOC_PARSER_ENABLE_ONNXRUNTIME
#include "image/oriented_page_view.h"
#include "image/page_image_cache.h"
#endif

#include <chrono>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace doc_parser::pipeline {
namespace {

using Clock = std::chrono::steady_clock;

long long elapsedMilliseconds(const Clock::time_point& started) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
}

// Stages overlap in lifecycle while pages are processed serially. Measure only
// time spent executing each stage, and emit one start/completion per document.
class PageStage {
public:
    PageStage(IStageObserver& observer, std::string name, std::string backend, int total)
        : observer_(observer), info_{std::move(name), std::move(backend), total} {}

    void begin() {
        if (!started_) {
            observer_.onStageStarted(info_);
            started_ = true;
        }
    }
    template <typename Operation> auto measure(Operation&& operation) {
        const auto started = Clock::now();
        auto result = operation();
        active_time_ += Clock::now() - started;
        return result;
    }
    void progress(int completed) { observer_.onStageProgress({info_.stage, completed, info_.total}); }
    void warnings(std::vector<common::Diagnostic> values) {
        for (auto& value : values) {
            observer_.onStageWarning(value);
            diagnostics.push_back(std::move(value));
        }
    }
    void complete() {
        if (!started_) {
            begin();
            progress(0);
        }
        observer_.onStageCompleted(
            {info_.stage, std::chrono::duration_cast<std::chrono::milliseconds>(active_time_).count()});
    }
    std::vector<common::Diagnostic> diagnostics;

private:
    IStageObserver& observer_;
    StageStartedInfo info_;
    Clock::duration active_time_{};
    bool started_ = false;
};

common::Status stageFailed(IStageObserver& observer,
                           const std::string& stage,
                           const std::string& code,
                           const std::string& message,
                           bool retryable = false) {
    spdlog::error("pipeline_failed: stage={} code={} reason={}", stage, code, message);
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

bool preprocessDebugImage(const PipelineContext& context, document::PageArtifact& page) {
    if (!context.debug) {
        return true;
    }

#if DOC_PARSER_ENABLE_OPENCV
    const image::ImagePreprocessor preprocessor;
    const std::filesystem::path output_path =
        context.output.debug_dir / ("page_" + std::to_string(page.page_number) + "_preprocessed.png");
    if (!preprocessor.preprocessToFile(image::readPageImage(page), output_path)) {
        spdlog::error("failed to preprocess image for page {}", page.page_number);
        return false;
    }

    page.debug_images.push_back({
        "preprocessed",
        relativeToOutputRoot(output_path, context),
        output_path,
    });
    spdlog::info("wrote: {}", output_path.string());
#else
    (void)page;
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

    const int page_count = document.source->pageCount();
    if (page_count < 0) {
        return stageFailed(observer, "render", "render.invalid_page_count", "document page count must be non-negative");
    }
    PageStage render_stage(observer,
                           "render",
                           resolvedBackend(run_provenance.backends.resolved.document, context.backends.document),
                           page_count);
    PageStage text_stage(
        observer, "text", resolvedBackend(run_provenance.backends.resolved.ocr, context.backends.ocr), page_count);
    PageStage layout_stage(observer,
                           "layout",
                           resolvedBackend(run_provenance.backends.resolved.layout, context.backends.layout),
                           page_count);
    PageStage table_stage(
        observer, "table", resolvedBackend(run_provenance.backends.resolved.table, context.backends.table), page_count);
    render_stage.begin();

    std::vector<document::PageArtifact> rendered_pages;
    std::vector<document::PageText> page_texts;
    std::vector<document::PageLayout> page_layouts;
    std::vector<document::PageTables> page_tables;
    std::vector<document::PageRotation> page_rotations;
    std::vector<std::filesystem::path> source_image_paths;
#if DOC_PARSER_ENABLE_OPENCV || DOC_PARSER_ENABLE_ONNXRUNTIME
    std::vector<std::unique_ptr<image::OrientedPageView>> orientation_views;
#endif
    rendered_pages.reserve(static_cast<std::size_t>(page_count));
    page_texts.reserve(static_cast<std::size_t>(page_count));
    page_layouts.reserve(static_cast<std::size_t>(page_count));
    page_tables.reserve(static_cast<std::size_t>(page_count));
    page_rotations.reserve(static_cast<std::size_t>(page_count));
    source_image_paths.reserve(static_cast<std::size_t>(page_count));

    document_source::RenderRequest render_request{context.render.dpi, context.output.root, context.output.pages_dir};
    const bool page_rendering = document.renderer->supportsPageRendering();
#if DOC_PARSER_ENABLE_OPENCV || DOC_PARSER_ENABLE_ONNXRUNTIME
    auto image_cache = std::make_shared<image::PageImageCache>(options.image_cache_bytes);
    if (page_rendering) {
        render_request.on_page_rendered = [weak_cache = std::weak_ptr<image::PageImageCache>(image_cache)](
                                              const document::PageArtifact& page, document::PageBitmap&& bitmap) {
            if (const auto cache = weak_cache.lock()) {
                cache->admitRendered(page, std::move(bitmap));
            }
        };
    }
#endif
    auto* native = document.native_text_extractor;
    const bool page_native_text = native != nullptr && native->supportsPageTextExtraction();
    spdlog::info("page_pipeline: pages={} incremental_render={} incremental_native_text={}",
                 page_count,
                 page_rendering,
                 page_native_text);
    std::vector<document::PageArtifact> legacy_pages;
    std::vector<document::PageText> legacy_texts;
    bool legacy_text_loaded = false;
    if (!page_rendering) {
        if (!render_stage.measure([&] { return document.renderer->renderPages(render_request, legacy_pages); })) {
            return stageFailed(observer, "render", "render_failed", "failed to render page artifacts", true);
        }
        if (legacy_pages.size() != static_cast<std::size_t>(page_count)) {
            return stageFailed(
                observer, "render", "render.page_count_mismatch", "rendered page count does not match the document");
        }
    }

    const TextExtractionStage text_extraction(native, *services->ocr);
    const LayoutAnalysisStage layout_analysis(*services->layout);
    const TableRecognitionStage table_recognition(*services->table);
    for (int index = 0; index < page_count; ++index) {
        const auto page_started = Clock::now();
        if (const auto deadline = deadlineStatus(options, run_started, observer, "render"); !deadline.okStatus()) {
            return deadline;
        }
        document::PageArtifact page;
        if (page_rendering) {
            if (!render_stage.measure([&] { return document.renderer->renderPage(render_request, index, page); })) {
                return stageFailed(
                    observer, "render", "render_failed", "failed to render page " + std::to_string(index + 1), true);
            }
        } else {
            page = std::move(legacy_pages[static_cast<std::size_t>(index)]);
        }
        if (page.page_index != index || page.page_number != index + 1) {
            return stageFailed(observer,
                               "render",
                               "render.page_identity_mismatch",
                               "rendered page identity does not match page " + std::to_string(index + 1));
        }
#if DOC_PARSER_ENABLE_OPENCV || DOC_PARSER_ENABLE_ONNXRUNTIME
        page.image_cache = image_cache;
#endif
        if (index == 0) {
            spdlog::info("page_pipeline: first_page_image_ms={}", elapsedMilliseconds(run_started));
        }
        observer.onArtifactReady({"render", "page_image", page.output_path, page.page_number});
        spdlog::info("wrote: {}", page.output_path.string());
        if (!render_stage.measure([&] { return preprocessDebugImage(context, page); })) {
            return stageFailed(observer, "render", "preprocess_failed", "failed to write debug preprocessing images");
        }
        render_stage.progress(index + 1);

        text_stage.begin();
        if (const auto deadline = deadlineStatus(options, run_started, observer, "text"); !deadline.okStatus()) {
            return deadline;
        }
        auto text_result = text_stage.measure([&]() -> PageTextExtractionResult {
            document::PageText native_text;
            native_text.page_index = index;
            native_text.page_number = index + 1;
            if (native != nullptr) {
                bool extracted = true;
                if (page_native_text) {
                    extracted = native->extractPageNativeText({context.render.dpi}, index, native_text);
                } else {
                    if (!legacy_text_loaded) {
                        extracted = native->extractNativeText({context.render.dpi}, legacy_texts);
                        legacy_text_loaded = true;
                    }
                    if (extracted && legacy_texts.size() != static_cast<std::size_t>(page_count)) {
                        PageTextExtractionResult failed;
                        failed.status = common::Status::error("text.page_count_mismatch",
                                                              "native text page count does not match page artifacts");
                        return failed;
                    }
                    if (extracted) {
                        native_text = std::move(legacy_texts[static_cast<std::size_t>(index)]);
                    }
                }
                if (!extracted) {
                    PageTextExtractionResult failed;
                    failed.status =
                        common::Status::error("text.native_extraction_failed",
                                              "native text extraction failed for page " + std::to_string(index + 1));
                    return failed;
                }
            }
            return text_extraction.extractPage(context, page, std::move(native_text));
        });
        if (!text_result.ok()) {
            return stageFailed(observer,
                               "text",
                               text_result.status.code(),
                               text_result.status.message(),
                               text_result.status.retryable());
        }
        source_image_paths.push_back(page.output_path);
        try {
            page_rotations.emplace_back(page.width, page.height, text_result.clockwise_correction_degrees);
        } catch (const std::invalid_argument& error) {
            return stageFailed(observer, "text", "text.orientation_invalid", error.what());
        }
        const auto& rotation = page_rotations.back();
        if (rotation.degrees() != 0) {
#if DOC_PARSER_ENABLE_OPENCV || DOC_PARSER_ENABLE_ONNXRUNTIME
            auto view = std::make_unique<image::OrientedPageView>();
            if (!text_stage.measure([&] { return view->prepare(page, rotation, context.output.root); })) {
                return stageFailed(
                    observer,
                    "text",
                    "text.orientation_failed",
                    "failed to prepare corrected page image for page " + std::to_string(page.page_number));
            }
            page = view->page();
            rotation.textToWorking(text_result.value);
            orientation_views.push_back(std::move(view));
            spdlog::debug("page_orientation: page={} correction={} working={}x{} source={}x{}",
                          page.page_number,
                          rotation.degrees(),
                          page.width,
                          page.height,
                          rotation.sourceWidth(),
                          rotation.sourceHeight());
#else
            return stageFailed(
                observer, "text", "text.orientation_unavailable", "corrected page images require OpenCV");
#endif
        }
        text_stage.warnings(std::move(text_result.diagnostics));
        text_stage.progress(index + 1);

        layout_stage.begin();
        if (const auto deadline = deadlineStatus(options, run_started, observer, "layout"); !deadline.okStatus()) {
            return deadline;
        }
        auto layout_result =
            layout_stage.measure([&] { return layout_analysis.analyzePage(context, page, text_result.value); });
        if (!layout_result.ok()) {
            return stageFailed(observer,
                               "layout",
                               layout_result.status.code(),
                               layout_result.status.message(),
                               layout_result.status.retryable());
        }
        layout_stage.warnings(std::move(layout_result.diagnostics));
        layout_stage.progress(index + 1);

        table_stage.begin();
        if (const auto deadline = deadlineStatus(options, run_started, observer, "table"); !deadline.okStatus()) {
            return deadline;
        }
        auto table_result = table_stage.measure(
            [&] { return table_recognition.recognizePage(context, page, text_result.value, layout_result.value); });
        if (!table_result.ok()) {
            return stageFailed(observer,
                               "table",
                               table_result.status.code(),
                               table_result.status.message(),
                               table_result.status.retryable());
        }
        table_stage.warnings(std::move(table_result.diagnostics));
#if DOC_PARSER_ENABLE_OPENCV || DOC_PARSER_ENABLE_ONNXRUNTIME
        spdlog::debug(
            "page_pipeline: page={} cache_resident_bytes={}", page.page_number, image_cache->stats().resident_bytes);
        image_cache->clear();
#endif
        rendered_pages.push_back(std::move(page));
        page_texts.push_back(std::move(text_result.value));
        page_layouts.push_back(std::move(layout_result.value));
        page_tables.push_back(std::move(table_result.value));
        table_stage.progress(index + 1);
        spdlog::debug("page_pipeline: page={} elapsed_ms={}", index + 1, elapsedMilliseconds(page_started));
    }

    // Events expose warnings immediately. Preserve the previous stage-major
    // order in exported warnings and fallback provenance for stable documents.
    NullStageObserver recorded_warnings;
    recordDiagnostics(text_stage.diagnostics, run_diagnostics, run_provenance, recorded_warnings);
    recordDiagnostics(layout_stage.diagnostics, run_diagnostics, run_provenance, recorded_warnings);
    recordDiagnostics(table_stage.diagnostics, run_diagnostics, run_provenance, recorded_warnings);
    render_stage.complete();
    text_stage.complete();
    layout_stage.complete();
    if (const auto deadline = deadlineStatus(options, run_started, observer, "table"); !deadline.okStatus()) {
        return deadline;
    }
    const auto link_status =
        table_stage.measure([&] { return TableRecognitionStage::linkPages(rendered_pages, page_tables); });
    if (!link_status.okStatus()) {
        return stageFailed(observer, "table", link_status.code(), link_status.message());
    }
    table_stage.complete();

#if DOC_PARSER_ENABLE_OPENCV || DOC_PARSER_ENABLE_ONNXRUNTIME
    const auto image_stats = image_cache->stats();
    spdlog::info("page_image_cache: budget_bytes={} resident_bytes={} peak_resident_bytes={} hits={} decodes={} "
                 "uncached_decodes={} failures={} decode_us={} rendered_admissions={} rendered_rejections={} "
                 "conversions={} conversion_us={}",
                 options.image_cache_bytes,
                 image_stats.resident_bytes,
                 image_stats.peak_resident_bytes,
                 image_stats.hits,
                 image_stats.decodes,
                 image_stats.uncached_decodes,
                 image_stats.failures,
                 image_stats.decode_microseconds,
                 image_stats.rendered_admissions,
                 image_stats.rendered_rejections,
                 image_stats.conversions,
                 image_stats.conversion_microseconds);
    image_cache.reset();
#endif

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
        std::move(rendered_pages),
        std::move(page_texts),
        std::move(page_layouts),
        std::move(page_reading_orders),
        std::move(page_tables),
    };
    assemble_request.source_size_bytes = source_fingerprint.size_bytes;
    assemble_request.source_sha256 = source_fingerprint.sha256;
    if (!document_assembler.assemble(std::move(assemble_request), parsed_document, artifacts)) {
        spdlog::error("document_assembly: failed to assemble document");
        return stageFailed(observer, "assembly", "assembly_failed", "failed to assemble document");
    }
    document::restoreSourceCoordinates(parsed_document, artifacts, page_rotations);
    for (std::size_t index = 0; index < artifacts.pages.size(); ++index) {
        artifacts.pages[index].image.output_path = std::move(source_image_paths[index]);
    }
    applyRunMetadata(options, run_diagnostics, run_provenance, parsed_document);
    observer.onStageProgress({"assembly", 1, 1});
    observer.onStageCompleted({"assembly", elapsedMilliseconds(stage_started)});
    std::size_t detected_furniture = 0;
    for (const auto& page : artifacts.pages) {
        for (const document::LayoutBlock& block : page.layout.blocks) {
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
