#pragma once

#include "backend/document_backend_interfaces.h"

#include <memory>
#include <string>

namespace doc_parser::backend {

struct DocumentBackendBundle {
    std::unique_ptr<pipeline::IDocumentSource> source;
    pipeline::IPageRenderer* renderer = nullptr;
    pipeline::INativeTextExtractor* native_text_extractor = nullptr;
};

DocumentBackendBundle createDocumentBackend(const std::string& backend_name);

} // namespace doc_parser::backend
