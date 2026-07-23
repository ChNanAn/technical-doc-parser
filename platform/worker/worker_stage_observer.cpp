#include "worker_stage_observer.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
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

} // namespace

WorkerStageObserver::WorkerStageObserver(RedisClient& redis,
                                         std::string job_id,
                                         std::string run_id,
                                         std::string attempt_id,
                                         std::filesystem::path run_directory)
    : redis_(redis), job_id_(std::move(job_id)), run_id_(std::move(run_id)), attempt_id_(std::move(attempt_id)),
      run_directory_(std::move(run_directory)), event_stream_("run-events:" + run_id_) {
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
    (void)redis_.addEvent(event_stream_, encoded);
    (void)redis_.addEvent("platform-events", encoded);
    redis_.setHash("run:" + run_id_, {{"last_event", encoded}, {"updated_at", event["timestamp"]}});
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
    std::ofstream output(manifest);
    output << artifact.dump(2) << '\n';
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
