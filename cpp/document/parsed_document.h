#pragma once

#include "document/document_block.h"
#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/reading_order_model.h"
#include "document/table_model.h"
#include "document/text_model.h"

#include <map>
#include <string>
#include <vector>

namespace doc_parser::document {

enum class DocumentStatus {
    Complete,
    Partial,
};

struct DocumentSource {
    std::string path;
    std::string type = "pdf";
    std::string filename;
    std::string media_type;
};

struct DocumentProducer {
    std::string name = "technical-doc-parser";
    std::string version = "unknown";
    std::string git_revision;
    std::string run_id;
};

struct DocumentPage {
    std::string id;
    int number = 0;
    double width = 0.0;
    double height = 0.0;
    std::string image_id;
    std::string image_uri;
    std::string image_media_type;
};

struct DocumentRelation {
    std::string id;
    std::string type;
    std::string from_block_id;
    std::string to_block_id;
};

struct DocumentWarning {
    std::string code;
    std::string message;
    std::string stage;
    std::string page_id;
    std::string block_id;
    std::map<std::string, std::string> details;
};

struct PipelinePageArtifacts {
    int page_index = 0;
    int page_number = 0;
    PageArtifact image;
    PageText text;
    PageLayout layout;
    PageReadingOrder reading_order;
    PageTables tables;
};

struct PipelineArtifacts {
    std::vector<PipelinePageArtifacts> pages;
};

struct ParsedDocument {
    DocumentSource source;
    int dpi = 200;
    std::vector<DocumentBlock> blocks;
    std::string document_id;
    DocumentStatus status = DocumentStatus::Complete;
    DocumentProducer producer;
    std::vector<DocumentPage> pages;
    std::vector<DocumentRelation> relations;
    std::vector<DocumentWarning> warnings;
};

} // namespace doc_parser::document
