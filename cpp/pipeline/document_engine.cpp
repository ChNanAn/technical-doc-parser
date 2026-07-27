#include "pipeline/document_engine.h"

#include "pipeline/document_pipeline.h"
#include "pipeline/pipeline_service_factory.h"

#include <utility>

namespace doc_parser::pipeline {
namespace {

PipelineRunOptions pipelineOptions(DocumentParseOptions options, const BackendOptions& backends) {
    PipelineRunOptions pipeline_options;
    static_cast<DocumentParseOptions&>(pipeline_options) = std::move(options);
    pipeline_options.backends = backends;
    return pipeline_options;
}

} // namespace

struct DocumentEngine::Impl {
    Impl(EngineConfig engine_config, const BackendRegistry& registry)
        : config(std::move(engine_config)), creation(createPipelineServices(config.backends, registry)) {}

    EngineConfig config;
    PipelineServiceCreationResult creation;
};

DocumentEngine::DocumentEngine(EngineConfig config) : DocumentEngine(config, createDefaultBackendRegistry(config)) {}

DocumentEngine::DocumentEngine(EngineConfig config, const BackendRegistry& registry)
    : impl_(std::make_unique<Impl>(std::move(config), registry)) {}

DocumentEngine::~DocumentEngine() = default;

DocumentEngine::DocumentEngine(DocumentEngine&&) noexcept = default;

DocumentEngine& DocumentEngine::operator=(DocumentEngine&&) noexcept = default;

bool DocumentEngine::isReady() const { return impl_ != nullptr && impl_->creation.status.okStatus(); }

const common::Status& DocumentEngine::initializationStatus() const {
    static const common::Status moved_from =
        common::Status::error("engine.moved_from", "document engine has no implementation", "configure");
    return impl_ == nullptr ? moved_from : impl_->creation.status;
}

ParseResult DocumentEngine::parse(DocumentParseOptions options) {
    NullStageObserver observer;
    return parse(std::move(options), observer);
}

ParseResult DocumentEngine::parse(DocumentParseOptions options, IStageObserver& observer) {
    ParseResult result;
    if (!isReady()) {
        result.status = initializationStatus();
        return result;
    }

    PipelineRunOptions pipeline_options = pipelineOptions(std::move(options), impl_->config.backends);
    const DocumentPipeline pipeline;
    result.status = pipeline.parse(pipeline_options,
                                   impl_->creation.services,
                                   impl_->creation.trace_message,
                                   result.document,
                                   result.artifacts,
                                   observer);
    return result;
}

} // namespace doc_parser::pipeline
