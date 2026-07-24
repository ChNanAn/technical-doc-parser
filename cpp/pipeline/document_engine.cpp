#include "pipeline/document_engine.h"

#include "pipeline/document_pipeline.h"
#include "pipeline/pipeline_service_factory.h"

#include <utility>

namespace doc_parser::pipeline {

struct DocumentEngine::Impl {
    Impl(BackendOptions backend_options, const BackendRegistry& registry)
        : options(std::move(backend_options)), creation(createPipelineServices(options, registry)) {}

    BackendOptions options;
    PipelineServiceCreationResult creation;
};

DocumentEngine::DocumentEngine(BackendOptions options)
    : DocumentEngine(std::move(options), createDefaultBackendRegistry()) {}

DocumentEngine::DocumentEngine(BackendOptions options, const BackendRegistry& registry)
    : impl_(std::make_unique<Impl>(std::move(options), registry)) {}

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

    options.backends = impl_->options;
    const DocumentPipeline pipeline;
    return pipeline.run(options, impl_->creation.services, impl_->creation.trace_message, observer);
}

} // namespace doc_parser::pipeline
