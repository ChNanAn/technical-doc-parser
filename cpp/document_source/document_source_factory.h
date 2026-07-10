#pragma once

#include "document_source/document_source_interfaces.h"

#include <memory>
#include <string>

namespace doc_parser::document_source {

struct DocumentSourceBundle {
    std::unique_ptr<pipeline::IDocumentSource> source;
    pipeline::IPageRenderer* renderer = nullptr;
    pipeline::INativeTextExtractor* native_text_extractor = nullptr;
};

DocumentSourceBundle createDocumentSource(const std::string& source_name);

} // namespace doc_parser::document_source
