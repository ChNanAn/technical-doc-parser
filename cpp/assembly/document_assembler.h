#pragma once

#include "document/parsed_document.h"

#include <string>
#include <vector>

namespace doc_parser::assembly {

struct DocumentAssembleRequest {
    std::string source_path;
    std::string source_type = "pdf";
    int dpi = 200;
    std::vector<document::PageArtifact> pages;
    std::vector<document::PageText> page_texts;
    std::vector<document::PageLayout> page_layouts;
    std::vector<document::PageTables> page_tables;
};

class DocumentAssembler {
public:
    bool assemble(const DocumentAssembleRequest& request, document::ParsedDocument& document) const;
};

} // namespace doc_parser::assembly
