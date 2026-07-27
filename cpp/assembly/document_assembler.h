#pragma once

#include "document/parsed_document.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace doc_parser::assembly {

struct DocumentAssembleRequest {
    DocumentAssembleRequest(std::string source_path_value,
                            std::string source_type_value,
                            int dpi_value,
                            std::vector<document::PageArtifact> pages_value,
                            std::vector<document::PageText> page_texts_value,
                            std::vector<document::PageLayout> page_layouts_value,
                            std::vector<document::PageReadingOrder> page_reading_orders_value,
                            std::vector<document::PageTables> page_tables_value,
                            std::optional<std::uintmax_t> source_size_bytes_value = std::nullopt,
                            std::string source_sha256_value = {})
        : source_path(std::move(source_path_value)), source_type(std::move(source_type_value)), dpi(dpi_value),
          pages(std::move(pages_value)), page_texts(std::move(page_texts_value)),
          page_layouts(std::move(page_layouts_value)), page_reading_orders(std::move(page_reading_orders_value)),
          page_tables(std::move(page_tables_value)), source_size_bytes(source_size_bytes_value),
          source_sha256(std::move(source_sha256_value)) {}

    std::string source_path;
    std::string source_type = "pdf";
    int dpi = 200;
    std::vector<document::PageArtifact> pages;
    std::vector<document::PageText> page_texts;
    std::vector<document::PageLayout> page_layouts;
    std::vector<document::PageReadingOrder> page_reading_orders;
    std::vector<document::PageTables> page_tables;
    std::optional<std::uintmax_t> source_size_bytes;
    std::string source_sha256;
};

class DocumentAssembler {
public:
    bool assemble(const DocumentAssembleRequest& request,
                  document::ParsedDocument& document,
                  document::PipelineArtifacts& artifacts) const;
};

} // namespace doc_parser::assembly
