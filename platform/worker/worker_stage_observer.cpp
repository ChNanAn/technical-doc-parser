#include "worker_stage_observer.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace doc_parser::platform {
namespace {

std::string timestamp() {
    const std::time_t value = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_r(&value, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::string mediaType(const std::string& kind) {
    if (kind == "page_image") {
        return "image/png";
    }
    if (kind == "document_markdown") {
        return "text/markdown";
    }
    if (kind == "document_html") {
        return "text/html";
    }
    return "application/json";
}

nlohmann::json backendOptions(const pipeline::BackendOptions& options) {
    return {
        {"document", options.document},
        {"ocr", options.ocr},
        {"layout", options.layout},
        {"table", options.table},
        {"registry_config", options.registry_config.string()},
    };
}

void writeJsonAtomically(const std::filesystem::path& destination, const nlohmann::json& value) {
    std::filesystem::path temporary = destination;
    temporary += ".tmp";

    std::error_code cleanup_error;
    std::filesystem::remove(temporary, cleanup_error);

    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            throw std::runtime_error("failed to open artifact manifest temporary file: " + temporary.string());
        }

        output << value.dump(2) << '\n';
        output.flush();
        if (!output) {
            throw std::runtime_error("failed to write artifact manifest temporary file: " + temporary.string());
        }
        output.close();
        if (!output) {
            throw std::runtime_error("failed to close artifact manifest temporary file: " + temporary.string());
        }

        std::error_code rename_error;
        std::filesystem::rename(temporary, destination, rename_error);
        if (rename_error) {
            throw std::runtime_error("failed to publish artifact manifest " + destination.string() + ": " +
                                     rename_error.message());
        }
    } catch (...) {
        std::filesystem::remove(temporary, cleanup_error);
        throw;
    }
}

} // namespace

WorkerStageObserver::WorkerStageObserver(IRedisEventWriter& redis,
                                         std::string job_id,
                                         std::string run_id,
                                         std::string attempt_id,
                                         std::filesystem::path run_directory,
                                         std::size_t run_event_stream_maximum_length,
                                         std::size_t platform_event_stream_maximum_length,
                                         int run_retention_seconds)
    : redis_(redis), job_id_(std::move(job_id)), run_id_(std::move(run_id)), attempt_id_(std::move(attempt_id)),
      run_directory_(std::move(run_directory)), event_stream_("run-events:" + run_id_),
      run_event_stream_maximum_length_(run_event_stream_maximum_length),
      platform_event_stream_maximum_length_(platform_event_stream_maximum_length),
      run_retention_seconds_(run_retention_seconds) {
    if (run_retention_seconds_ <= 0) {
        throw std::invalid_argument("run retention must be positive");
    }
    std::filesystem::create_directories(run_directory_ / "artifacts");
}

void WorkerStageObserver::publish(nlohmann::json event) {
    ++sequence_;
    event["schema_version"] = 1;
    event["event_id"] = "evt_" + run_id_ + "_" + std::to_string(sequence_);
    event["job_id"] = job_id_;
    event["run_id"] = run_id_;
    event["attempt_id"] = attempt_id_;
    event["sequence"] = sequence_;
    event["timestamp"] = timestamp();
    const std::string encoded = event.dump();
    (void)redis_.addEvent(event_stream_, encoded, run_event_stream_maximum_length_);
    redis_.expire(event_stream_, run_retention_seconds_);
    (void)redis_.addEvent("platform-events", encoded, platform_event_stream_maximum_length_);
    const std::string run_key = "run:" + run_id_;
    redis_.setHash(run_key, {{"last_event", encoded}, {"updated_at", event["timestamp"]}});
    redis_.expire(run_key, run_retention_seconds_);
    std::ofstream log(run_directory_ / "events.ndjson", std::ios::app);
    log << encoded << '\n';
}

void WorkerStageObserver::publishJobEvent(const std::string& type, const std::string& message) {
    nlohmann::json event{{"type", type}};
    const std::string effective_error = message.empty() && type == "job_failed" ? last_error_ : message;
    if (!effective_error.empty()) {
        const bool uses_stage_error = message.empty() && !last_error_.empty();
        event["error"] = {
            {"code", uses_stage_error ? last_error_code_ : "worker_failure"},
            {"message", effective_error},
            {"retryable", uses_stage_error && last_error_retryable_},
        };
    }
    publish(std::move(event));
    std::string status = "running";
    if (type == "job_succeeded") {
        status = "succeeded";
    } else if (type == "job_failed") {
        status = "failed";
    } else if (type == "job_cancelled") {
        status = "cancelled";
    }
    std::map<std::string, std::string> state{{"status", status}};
    if (!effective_error.empty()) {
        state["error"] = effective_error;
    }
    redis_.setHash("run:" + run_id_, state);
}

void WorkerStageObserver::onRunConfigured(const pipeline::RunProvenance& provenance) {
    nlohmann::json models = nlohmann::json::array();
    for (const pipeline::ModelProvenance& model : provenance.models) {
        nlohmann::json value{
            {"stage", model.stage},
            {"backend", model.backend},
            {"role", model.role},
            {"path", model.path.string()},
        };
        if (!model.profile.empty()) {
            value["profile"] = model.profile;
        }
        models.push_back(std::move(value));
    }
    publish({
        {"type", "run_configured"},
        {"engine",
         {
             {"name", provenance.engine_name},
             {"version", provenance.engine_version},
             {"git_revision", provenance.git_revision},
         }},
        {"backends",
         {
             {"requested", backendOptions(provenance.backends.requested)},
             {"resolved", backendOptions(provenance.backends.resolved)},
             {"config_source", provenance.backends.config_source},
         }},
        {"models", std::move(models)},
    });
}

void WorkerStageObserver::onStageWarning(const common::Diagnostic& diagnostic) {
    nlohmann::json event{
        {"type", "stage_warning"},
        {"stage", diagnostic.stage},
        {"warning",
         {
             {"code", diagnostic.code},
             {"message", diagnostic.message},
             {"details", diagnostic.details},
         }},
    };
    if (diagnostic.page_number > 0) {
        event["page_number"] = diagnostic.page_number;
    }
    publish(std::move(event));
}

void WorkerStageObserver::onStageStarted(const pipeline::StageStartedInfo& info) {
    publish({{"type", "stage_started"}, {"stage", info.stage}, {"backend", info.backend}});
    redis_.setHash("run:" + run_id_, {{"status", "running"}, {"stage", info.stage}});
}

void WorkerStageObserver::onStageProgress(const pipeline::StageProgressInfo& info) {
    publish({
        {"type", "stage_progress"},
        {"stage", info.stage},
        {"progress", {{"completed", info.completed}, {"total", info.total}}},
    });
}

void WorkerStageObserver::onArtifactReady(const pipeline::StageArtifactInfo& info) {
    std::string artifact_id = "artifact_" + info.stage + "_" + info.kind;
    if (info.page_number > 0) {
        artifact_id += "_page_" + std::to_string(info.page_number);
    }
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(info.path, error);
    nlohmann::json artifact{
        {"schema_version", 1},
        {"artifact_id", artifact_id},
        {"job_id", job_id_},
        {"run_id", run_id_},
        {"attempt_id", attempt_id_},
        {"stage", info.stage},
        {"kind", info.kind},
        {"uri", "file://" + std::filesystem::absolute(info.path).string()},
        {"media_type", mediaType(info.kind)},
        {"size_bytes", error ? 0 : size},
        {"created_at", timestamp()},
    };
    if (info.page_number > 0) {
        artifact["page_number"] = info.page_number;
    }
    const std::filesystem::path manifest = run_directory_ / "artifacts" / (artifact_id + ".json");
    writeJsonAtomically(manifest, artifact);
    publish({
        {"type", "artifact_ready"},
        {"stage", info.stage},
        {"artifact_id", artifact_id},
    });
}

void WorkerStageObserver::onStageCompleted(const pipeline::StageCompletedInfo& info) {
    publish({{"type", "stage_completed"}, {"stage", info.stage}, {"duration_ms", info.duration_ms}});
}

void WorkerStageObserver::onStageFailed(const pipeline::StageFailedInfo& info) {
    last_error_code_ = info.code;
    last_error_ = info.message;
    last_error_retryable_ = info.retryable;
    publish({
        {"type", "stage_failed"},
        {"stage", info.stage},
        {"error", {{"code", info.code}, {"message", info.message}, {"retryable", info.retryable}}},
    });
}

} // namespace doc_parser::platform
