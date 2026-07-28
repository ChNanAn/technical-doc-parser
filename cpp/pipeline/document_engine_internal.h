#pragma once

#include "pipeline/backend_registry.h"

#include <document_intelligence_engine/document_engine.h>
#include <memory>

namespace doc_parser::pipeline {

class DocumentEngineInternalAccess {
public:
    static DocumentEngine create(EngineConfig config, const BackendRegistry& registry);
    static std::unique_ptr<DocumentEngine> createUnique(EngineConfig config, const BackendRegistry& registry);
};

} // namespace doc_parser::pipeline
