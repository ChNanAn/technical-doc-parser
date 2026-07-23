#pragma once

#include <filesystem>
#include <string>

namespace doc_parser::pipeline {

struct StageStartedInfo {
    std::string stage;
    std::string backend;
    int total = 0;
};

struct StageProgressInfo {
    std::string stage;
    int completed = 0;
    int total = 0;
};

struct StageArtifactInfo {
    std::string stage;
    std::string kind;
    std::filesystem::path path;
    int page_number = 0;
};

struct StageCompletedInfo {
    std::string stage;
    long long duration_ms = 0;
};

struct StageFailedInfo {
    std::string stage;
    std::string code;
    std::string message;
    bool retryable = false;
};

class IStageObserver {
public:
    virtual ~IStageObserver() = default;

    virtual void onStageStarted(const StageStartedInfo& info) = 0;
    virtual void onStageProgress(const StageProgressInfo& info) = 0;
    virtual void onArtifactReady(const StageArtifactInfo& info) = 0;
    virtual void onStageCompleted(const StageCompletedInfo& info) = 0;
    virtual void onStageFailed(const StageFailedInfo& info) = 0;
};

class NullStageObserver final : public IStageObserver {
public:
    void onStageStarted(const StageStartedInfo&) override {}
    void onStageProgress(const StageProgressInfo&) override {}
    void onArtifactReady(const StageArtifactInfo&) override {}
    void onStageCompleted(const StageCompletedInfo&) override {}
    void onStageFailed(const StageFailedInfo&) override {}
};

} // namespace doc_parser::pipeline
