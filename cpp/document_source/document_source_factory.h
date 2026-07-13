#pragma once

#include "document_source/document_source_interfaces.h"

#include <memory>
#include <string>

namespace doc_parser::document_source {

struct DocumentSourceBundle {
    std::unique_ptr<IDocumentSource> source;
    IPageRenderer* renderer = nullptr;
    INativeTextExtractor* native_text_extractor = nullptr;
};

DocumentSourceBundle createDocumentSource(const std::string& source_name);

} // namespace doc_parser::document_source
