#pragma once

#include "common/status.h"

#include "document/parsed_document.h"
#include "pipeline/pipeline_options.h"
#include "pipeline/run_provenance.h"
#include "pipeline/stage_observer.h"

namespace doc_parser::pipeline {

class BackendRegistry;
class DocumentEngine;
struct PipelineServices;

class DocumentPipeline {
public:
    common::Status run(const PipelineRunOptions& options) const = delete;
    common::Status run(const PipelineRunOptions& options, IStageObserver& observer) const = delete;
    common::Status
    run(const PipelineRunOptions& options, const BackendRegistry& registry, IStageObserver& observer) const;

private:
    friend class DocumentEngine;

    common::Status parse(const PipelineRunOptions& options,
                         PipelineServices& services,
                         const RunProvenance& service_provenance,
                         document::ParsedDocument& document,
                         document::PipelineArtifacts& artifacts,
                         RunProvenance& run_provenance,
                         IStageObserver& observer) const;
    common::Status parseInternal(const PipelineRunOptions& options,
                                 PipelineServices* services,
                                 const RunProvenance* service_provenance,
                                 document::ParsedDocument& document,
                                 document::PipelineArtifacts& artifacts,
                                 RunProvenance& run_provenance,
                                 IStageObserver& observer,
                                 const BackendRegistry* registry) const;
    common::Status exportResult(const PipelineRunOptions& options,
                                const document::ParsedDocument& document,
                                const document::PipelineArtifacts& artifacts,
                                IStageObserver& observer) const;
};

} // namespace doc_parser::pipeline
