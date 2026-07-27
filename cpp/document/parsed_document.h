#pragma once

#include "document/document_block.h"
#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/reading_order_model.h"
#include "document/table_model.h"
#include "document/text_model.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
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
    std::optional<std::uintmax_t> size_bytes;
    std::string sha256;
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
    DocumentWarning() = default;

    DocumentWarning(std::string code_value,
                    std::string message_value,
                    std::string stage_value = {},
                    std::string page_id_value = {},
                    std::string block_id_value = {},
                    std::map<std::string, std::string> details_value = {},
                    std::size_t occurrence_count_value = 1,
                    std::vector<std::string> page_ids_value = {})
        : code(std::move(code_value)), message(std::move(message_value)), stage(std::move(stage_value)),
          page_id(std::move(page_id_value)), block_id(std::move(block_id_value)), details(std::move(details_value)),
          occurrence_count(occurrence_count_value), page_ids(std::move(page_ids_value)) {}

    std::string code;
    std::string message;
    std::string stage;
    std::string page_id;
    std::string block_id;
    std::map<std::string, std::string> details;
    std::size_t occurrence_count = 1;
    std::vector<std::string> page_ids;
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
