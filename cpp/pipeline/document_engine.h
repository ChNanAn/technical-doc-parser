#pragma once

#include "pipeline/backend_registry.h"
#include "pipeline/pipeline_options.h"
#include "pipeline/stage_observer.h"

#include <memory>
#include <string>

namespace doc_parser::pipeline {

// Reusable document engine. One instance owns its model backends and is intended
// for sequential parsing; create one instance per concurrently parsing thread.
class DocumentEngine {
public:
    explicit DocumentEngine(BackendOptions options = {});
    DocumentEngine(BackendOptions options, const BackendRegistry& registry);
    ~DocumentEngine();

    DocumentEngine(const DocumentEngine&) = delete;
    DocumentEngine& operator=(const DocumentEngine&) = delete;
    DocumentEngine(DocumentEngine&&) noexcept;
    DocumentEngine& operator=(DocumentEngine&&) noexcept;

    bool isReady() const;
    const std::string& initializationError() const;

    bool parse(PipelineRunOptions options);
    bool parse(PipelineRunOptions options, IStageObserver& observer);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace doc_parser::pipeline
