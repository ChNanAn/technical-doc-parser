#pragma once

#include "pipeline/pipeline_options.h"
#include "pipeline/stage_observer.h"

namespace doc_parser::pipeline {

class DocumentEngine;
struct PipelineServices;

class DocumentPipeline {
public:
    bool run(const PipelineRunOptions& options) const;
    bool run(const PipelineRunOptions& options, IStageObserver& observer) const;

private:
    friend class DocumentEngine;

    bool run(const PipelineRunOptions& options,
             PipelineServices& services,
             const std::string& service_trace,
             IStageObserver& observer) const;
    bool runInternal(const PipelineRunOptions& options,
                     PipelineServices* services,
                     const std::string& service_trace,
                     IStageObserver& observer) const;
};

} // namespace doc_parser::pipeline
