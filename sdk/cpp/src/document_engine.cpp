#include "document_intelligence_engine/document_engine.h"

#include "pipeline/document_engine_internal.h"
#include "pipeline/document_pipeline.h"
#include "pipeline/pipeline_service_factory.h"

#include <atomic>
#include <exception>
#include <utility>

namespace doc_parser::pipeline {
namespace {

PipelineRunOptions pipelineOptions(DocumentParseOptions options, const BackendOptions& backends) {
    PipelineRunOptions pipeline_options;
    static_cast<DocumentParseOptions&>(pipeline_options) = std::move(options);
    pipeline_options.backends = backends;
    return pipeline_options;
}

class ParseLease {
public:
    explicit ParseLease(std::atomic<bool>& parsing) : parsing_(parsing) {}
    ~ParseLease() { parsing_.store(false); }

    ParseLease(const ParseLease&) = delete;
    ParseLease& operator=(const ParseLease&) = delete;

private:
    std::atomic<bool>& parsing_;
};

} // namespace

struct DocumentEngine::Impl {
    Impl(EngineConfig engine_config, const BackendRegistry& registry)
        : config(std::move(engine_config)), creation(createPipelineServices(config, registry)) {}

    EngineConfig config;
    PipelineServiceCreationResult creation;
    std::atomic<bool> parsing{false};
};

DocumentEngine::DocumentEngine(EngineConfig config)
    : DocumentEngine(std::make_unique<Impl>(config, createDefaultBackendRegistry(config))) {}

DocumentEngine::DocumentEngine(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

DocumentEngine::~DocumentEngine() = default;

DocumentEngine::DocumentEngine(DocumentEngine&&) noexcept = default;

DocumentEngine& DocumentEngine::operator=(DocumentEngine&&) noexcept = default;

bool DocumentEngine::isReady() const { return impl_ != nullptr && impl_->creation.status.okStatus(); }

DocumentEngineState DocumentEngine::state() const {
    if (impl_ == nullptr) {
        return DocumentEngineState::MovedFrom;
    }
    if (!impl_->creation.status.okStatus()) {
        return DocumentEngineState::InitializationFailed;
    }
    return impl_->parsing.load() ? DocumentEngineState::Parsing : DocumentEngineState::Ready;
}

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

    bool expected = false;
    if (!impl_->parsing.compare_exchange_strong(expected, true)) {
        result.status = common::Status::error(
            "engine.busy", "document engine is already parsing another document", "engine", true);
        return result;
    }
    const ParseLease parse_lease(impl_->parsing);

    try {
        PipelineRunOptions pipeline_options = pipelineOptions(std::move(options), impl_->config.backends);
        const DocumentPipeline pipeline;
        result.status = pipeline.parse(pipeline_options,
                                       impl_->creation.services,
                                       impl_->creation.provenance,
                                       result.document,
                                       result.artifacts,
                                       result.provenance,
                                       observer);
    } catch (const std::exception& error) {
        result.status = common::Status::error(
            "engine.parse_exception", "document parsing raised an exception: " + std::string(error.what()), "engine");
    } catch (...) {
        result.status =
            common::Status::error("engine.parse_exception", "document parsing raised an unknown exception", "engine");
    }
    return result;
}

DocumentEngine DocumentEngineInternalAccess::create(EngineConfig config, const BackendRegistry& registry) {
    return DocumentEngine(std::make_unique<DocumentEngine::Impl>(std::move(config), registry));
}

std::unique_ptr<DocumentEngine>
DocumentEngineInternalAccess::createUnique(EngineConfig config, const BackendRegistry& registry) {
    return std::unique_ptr<DocumentEngine>(
        new DocumentEngine(std::make_unique<DocumentEngine::Impl>(std::move(config), registry)));
}

} // namespace doc_parser::pipeline
