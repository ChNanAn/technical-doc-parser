#pragma once

#include "pipeline/backend_registry.h"
#include "pipeline/document_engine.h"
#include "pipeline/engine_config.h"

#include <cstddef>
#include <list>
#include <memory>

namespace doc_parser::platform {

pipeline::BackendOptions effectiveBackendOptions(const pipeline::BackendOptions& defaults,
                                                 const pipeline::BackendOptions& requested);

struct DocumentEngineLookup {
    pipeline::DocumentEngine* engine = nullptr;
    bool cache_hit = false;
};

class DocumentEngineCache {
public:
    DocumentEngineCache(pipeline::EngineConfig base_config,
                        const pipeline::BackendRegistry& registry,
                        std::size_t capacity);

    DocumentEngineLookup get(const pipeline::BackendOptions& backends);
    std::size_t size() const;
    std::size_t capacity() const;

private:
    struct Entry {
        pipeline::BackendOptions backends;
        std::unique_ptr<pipeline::DocumentEngine> engine;
    };

    pipeline::EngineConfig base_config_;
    const pipeline::BackendRegistry& registry_;
    std::size_t capacity_;
    std::list<Entry> entries_;
};

} // namespace doc_parser::platform
