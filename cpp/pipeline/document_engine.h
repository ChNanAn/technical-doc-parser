#pragma once

#include "common/status.h"

#include "document/parsed_document.h"
#include "pipeline/engine_config.h"
#include "pipeline/pipeline_options.h"
#include "pipeline/run_provenance.h"
#include "pipeline/stage_observer.h"

#include <memory>
#include <string>

namespace doc_parser::pipeline {

class BackendRegistry;

enum class DocumentEngineState {
    Ready,
    Parsing,
    InitializationFailed,
    MovedFrom,
};

struct ParseResult {
    common::Status status =
        common::Status::error("engine.not_started", "document parsing has not started", "engine");
    document::ParsedDocument document;
    document::PipelineArtifacts artifacts;
    RunProvenance provenance;

    bool ok() const { return status.okStatus(); }
};

// Reusable document engine. One instance owns its model backends and is intended
// for sequential parsing; create one instance per concurrently parsing thread.
class DocumentEngine {
public:
    DocumentEngine() = delete;
    explicit DocumentEngine(EngineConfig config);
    DocumentEngine(EngineConfig config, const BackendRegistry& registry);
    ~DocumentEngine();

    DocumentEngine(const DocumentEngine&) = delete;
    DocumentEngine& operator=(const DocumentEngine&) = delete;
    DocumentEngine(DocumentEngine&&) noexcept;
    DocumentEngine& operator=(DocumentEngine&&) noexcept;

    bool isReady() const;
    DocumentEngineState state() const;
    const common::Status& initializationStatus() const;

    ParseResult parse(DocumentParseOptions options);
    ParseResult parse(DocumentParseOptions options, IStageObserver& observer);

    ParseResult parse(PipelineRunOptions options) = delete;
    ParseResult parse(PipelineRunOptions options, IStageObserver& observer) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace doc_parser::pipeline
