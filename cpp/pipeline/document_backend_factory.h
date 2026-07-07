#pragma once

#include "pipeline/stage_interfaces.h"

#include <memory>
#include <string>

namespace doc_parser::pipeline {

struct DocumentBackendBundle {
    std::unique_ptr<IDocumentSource> source;
    IPageRenderer* renderer = nullptr;
    INativeTextExtractor* native_text_extractor = nullptr;
};

DocumentBackendBundle createDocumentBackend(const std::string& backend_name);

} // namespace doc_parser::pipeline
