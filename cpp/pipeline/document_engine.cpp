#include "pipeline/document_engine.h"

#include "pipeline/document_pipeline.h"
#include "pipeline/pipeline_service_factory.h"

#include <utility>

namespace doc_parser::pipeline {

struct DocumentEngine::Impl {
    Impl(EngineConfig engine_config, const BackendRegistry& registry)
        : config(std::move(engine_config)), creation(createPipelineServices(config.backends, registry)) {}

    EngineConfig config;
    PipelineServiceCreationResult creation;
};

DocumentEngine::DocumentEngine() : DocumentEngine(defaultEngineConfig()) {}

DocumentEngine::DocumentEngine(EngineConfig config) : DocumentEngine(config, createDefaultBackendRegistry(config)) {}

DocumentEngine::DocumentEngine(EngineConfig config, const BackendRegistry& registry)
    : impl_(std::make_unique<Impl>(std::move(config), registry)) {}

DocumentEngine::~DocumentEngine() = default;

DocumentEngine::DocumentEngine(DocumentEngine&&) noexcept = default;

DocumentEngine& DocumentEngine::operator=(DocumentEngine&&) noexcept = default;

bool DocumentEngine::isReady() const { return impl_ != nullptr && impl_->creation.ok; }

const std::string& DocumentEngine::initializationError() const {
    static const std::string empty;
    return impl_ == nullptr ? empty : impl_->creation.error_message;
}

bool DocumentEngine::parse(PipelineRunOptions options) {
    NullStageObserver observer;
    return parse(std::move(options), observer);
}

bool DocumentEngine::parse(PipelineRunOptions options, IStageObserver& observer) {
    if (!isReady()) {
        return false;
    }

    options.backends = impl_->config.backends;
    const DocumentPipeline pipeline;
    return pipeline.run(options, impl_->creation.services, impl_->creation.trace_message, observer);
}

} // namespace doc_parser::pipeline
