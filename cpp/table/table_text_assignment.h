#pragma once

#include "document/table_model.h"
#include "document/text_model.h"

#include <cstddef>
#include <vector>

namespace doc_parser::table::detail {

struct TableTextToken {
    std::string text;
    document::BBox bbox;
    double confidence = 1.0;
};

struct TableTextAssignmentStats {
    std::size_t assigned_tokens = 0;
    std::size_t ambiguous_tokens = 0;
    std::size_t unassigned_tokens = 0;
};

std::vector<TableTextToken> collectTableTextTokens(const document::PageText& text);

// Populate a fresh grid. Each source token can belong to at most one cell in this
// table; equal strings at different source positions remain distinct tokens.
TableTextAssignmentStats assignTableText(document::Table& table, const std::vector<TableTextToken>& tokens);

} // namespace doc_parser::table::detail
