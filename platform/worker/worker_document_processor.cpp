#include "worker_document_processor.h"

#include "pipeline/document_engine.h"

#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

namespace doc_parser::platform {
namespace {

using Clock = std::chrono::steady_clock;

long long elapsedMilliseconds(const Clock::time_point& started) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
}

common::Status stageFailed(pipeline::IStageObserver& observer,
                           const std::string& stage,
                           const std::string& code,
                           const std::string& message,
                           bool retryable = false) {
    observer.onStageFailed({stage, code, message, retryable});
    return common::Status::error(code, message, stage, retryable);
}

} // namespace

WorkerDocumentProcessor::WorkerDocumentProcessor(pipeline::EngineConfig base_config,
                                                 const pipeline::BackendRegistry& registry,
                                                 std::size_t cache_capacity)
    : engines_(std::move(base_config), registry, cache_capacity), exporter_(exporter::createDefaultDocumentExporter()) {
}

common::Status WorkerDocumentProcessor::process(const pipeline::DocumentParseOptions& options,
                                                const pipeline::BackendOptions& backends,
                                                pipeline::IStageObserver& observer) {
    const Clock::time_point run_started = Clock::now();
    const DocumentEngineLookup lookup = engines_.get(backends);
    if (!lookup.ok()) {
        const common::Status& status = lookup.status;
        observer.onStageStarted({"configure", "registry", 1});
        return stageFailed(observer,
                           status.stage().empty() ? "configure" : status.stage(),
                           status.code(),
                           status.message(),
                           status.retryable());
    }

    pipeline::ParseResult result = lookup.engine->parse(options, observer);
    if (!result.ok()) {
        return result.status;
    }
    if (options.timeout_seconds > 0 && Clock::now() - run_started >= std::chrono::seconds(options.timeout_seconds)) {
        return stageFailed(observer,
                           "export",
                           "run_timeout",
                           "pipeline exceeded its " + std::to_string(options.timeout_seconds) + " second deadline");
    }

    const Clock::time_point export_started = Clock::now();
    observer.onStageStarted({"export", "multi-format", 3});
    if (exporter_ == nullptr) {
        spdlog::error("export: no document exporter is enabled");
        return stageFailed(observer, "export", "exporter_unavailable", "no document exporter is enabled");
    }

    const std::filesystem::path json_path = options.output_directory / "document.json";
    const common::Status export_status =
        exporter_->write({options.debug, json_path, &result.document, &result.artifacts});
    if (!export_status.okStatus()) {
        spdlog::error("export failed [{}]: {}", export_status.code(), export_status.message());
        return stageFailed(observer,
                           export_status.stage().empty() ? "export" : export_status.stage(),
                           export_status.code(),
                           export_status.message(),
                           export_status.retryable());
    }

    std::filesystem::path markdown_path = json_path;
    std::filesystem::path html_path = json_path;
    markdown_path.replace_extension(".md");
    html_path.replace_extension(".html");
    observer.onArtifactReady({"export", "document_json", json_path, 0});
    observer.onArtifactReady({"export", "document_markdown", markdown_path, 0});
    observer.onArtifactReady({"export", "document_html", html_path, 0});
    observer.onStageProgress({"export", 3, 3});
    observer.onStageCompleted({"export", elapsedMilliseconds(export_started)});
    spdlog::info("wrote: {}", json_path.string());
    spdlog::info("wrote: {}", markdown_path.string());
    spdlog::info("wrote: {}", html_path.string());
    return common::Status::ok();
}

std::size_t WorkerDocumentProcessor::cachedEngineCount() const { return engines_.size(); }

} // namespace doc_parser::platform
