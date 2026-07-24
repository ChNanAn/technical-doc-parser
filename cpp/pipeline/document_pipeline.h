#pragma once

#include "pipeline/pipeline_options.h"
#include "pipeline/stage_observer.h"

namespace doc_parser::pipeline {

class DocumentPipeline {
public:
    bool run(const PipelineRunOptions& options) const;
    bool run(const PipelineRunOptions& options, IStageObserver& observer) const;
};

} // namespace doc_parser::pipeline
