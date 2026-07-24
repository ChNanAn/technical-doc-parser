#pragma once

#include "common/status.h"

#include "pipeline/pipeline_options.h"
#include "pipeline/stage_observer.h"

namespace doc_parser::pipeline {

class DocumentEngine;
struct PipelineServices;

class DocumentPipeline {
public:
    common::Status run(const PipelineRunOptions& options) const;
    common::Status run(const PipelineRunOptions& options, IStageObserver& observer) const;

private:
    friend class DocumentEngine;

    common::Status run(const PipelineRunOptions& options,
                       PipelineServices& services,
                       const std::string& service_trace,
                       IStageObserver& observer) const;
    common::Status runInternal(const PipelineRunOptions& options,
                               PipelineServices* services,
                               const std::string& service_trace,
                               IStageObserver& observer) const;
};

} // namespace doc_parser::pipeline
