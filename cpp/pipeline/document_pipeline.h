#pragma once

#include "common/status.h"

#include "document/parsed_document.h"
#include "pipeline/pipeline_options.h"
#include "pipeline/stage_observer.h"

namespace doc_parser::pipeline {

class BackendRegistry;
class DocumentEngine;
struct PipelineServices;

class DocumentPipeline {
public:
    common::Status run(const PipelineRunOptions& options) const;
    common::Status run(const PipelineRunOptions& options, IStageObserver& observer) const;
    common::Status
    run(const PipelineRunOptions& options, const BackendRegistry& registry, IStageObserver& observer) const;

private:
    friend class DocumentEngine;

    common::Status
    runWithRegistry(const PipelineRunOptions& options, const BackendRegistry* registry, IStageObserver& observer) const;

    common::Status parse(const PipelineRunOptions& options,
                         PipelineServices& services,
                         const std::string& service_trace,
                         document::ParsedDocument& document,
                         document::PipelineArtifacts& artifacts,
                         IStageObserver& observer) const;
    common::Status parseInternal(const PipelineRunOptions& options,
                                 PipelineServices* services,
                                 const std::string& service_trace,
                                 document::ParsedDocument& document,
                                 document::PipelineArtifacts& artifacts,
                                 IStageObserver& observer,
                                 const BackendRegistry* registry) const;
    common::Status exportResult(const PipelineRunOptions& options,
                                const document::ParsedDocument& document,
                                const document::PipelineArtifacts& artifacts,
                                IStageObserver& observer) const;
};

} // namespace doc_parser::pipeline
