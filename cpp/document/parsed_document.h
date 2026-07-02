#pragma once

#include "document/page_artifact.h"
#include "document/text_model.h"

#include <string>
#include <vector>

namespace doc_parser::document {

struct DocumentSource {
    std::string path;
    std::string type = "pdf";
};

struct ParsedPage {
    int page_index = 0;
    int page_number = 0;
    PageArtifact image;
    PageText text;
};

struct ParsedDocument {
    DocumentSource source;
    int dpi = 200;
    std::vector<ParsedPage> pages;
};

} // namespace doc_parser::document
