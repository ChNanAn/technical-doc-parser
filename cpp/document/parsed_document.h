#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/table_model.h"
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
    PageLayout layout;
    PageTables tables;
};

struct ParsedDocument {
    DocumentSource source;
    int dpi = 200;
    std::vector<ParsedPage> pages;
};

} // namespace doc_parser::document
