#pragma once

#include "pipeline/stage_observer.h"
#include "redis_client.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace doc_parser::platform {

class WorkerStageObserver final : public pipeline::IStageObserver {
public:
    WorkerStageObserver(RedisClient& redis,
                        std::string job_id,
                        std::string run_id,
                        std::string attempt_id,
                        std::filesystem::path run_directory);

    void publishJobEvent(const std::string& type, const std::string& message = {});
    void onStageStarted(const pipeline::StageStartedInfo& info) override;
    void onStageProgress(const pipeline::StageProgressInfo& info) override;
    void onArtifactReady(const pipeline::StageArtifactInfo& info) override;
    void onStageCompleted(const pipeline::StageCompletedInfo& info) override;
    void onStageFailed(const pipeline::StageFailedInfo& info) override;

private:
    void publish(nlohmann::json event);

    RedisClient& redis_;
    std::string job_id_;
    std::string run_id_;
    std::string attempt_id_;
    std::filesystem::path run_directory_;
    std::string event_stream_;
    std::string last_error_code_;
    std::string last_error_;
    bool last_error_retryable_ = false;
    int sequence_ = 0;
};

} // namespace doc_parser::platform
