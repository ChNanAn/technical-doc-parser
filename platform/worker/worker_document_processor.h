#pragma once

#include "common/status.h"

#include "document_engine_cache.h"
#include "export/document_exporter.h"
#include "pipeline/pipeline_options.h"
#include "pipeline/stage_observer.h"

#include <cstddef>
#include <memory>

namespace doc_parser::platform {

class WorkerDocumentProcessor {
public:
    WorkerDocumentProcessor(pipeline::EngineConfig base_config,
                            const pipeline::BackendRegistry& registry,
                            std::size_t cache_capacity);

    common::Status process(const pipeline::DocumentParseOptions& options,
                           const pipeline::BackendOptions& backends,
                           pipeline::IStageObserver& observer);

    std::size_t cachedEngineCount() const;

private:
    DocumentEngineCache engines_;
    std::unique_ptr<exporter::IDocumentExporter> exporter_;
};

} // namespace doc_parser::platform
