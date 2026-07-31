#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/text_model.h"

#include <vector>

namespace doc_parser::layout::detail {

struct LayoutRefinementStats {
    int reordered_blocks = 0;
    int maximum_columns = 1;
};

struct LayoutRecoveryStats {
    int recovered_lines = 0;
    int recovered_furniture_lines = 0;
    int preserved_edge_body_lines = 0;
    int attached_lines = 0;
    int attached_groups = 0;
    int coalesced_grid_groups = 0;
    int skipped_marginalia_lines = 0;
    int fallback_blocks = 0;
    int skipped_furniture_lines = 0;
};

struct EdgeFurnitureRefinementStats {
    int headers = 0;
    int footers = 0;
};

void assignTextLines(const document::PageText& text, std::vector<document::LayoutBlock>& blocks);
void associateCaptions(const document::PageArtifact& page, std::vector<document::LayoutBlock>& blocks);
LayoutRefinementStats refineMultiColumnTextLineOrder(const document::PageText& text,
                                                     std::vector<document::LayoutBlock>& blocks);
EdgeFurnitureRefinementStats refineEdgeFurniture(const document::PageArtifact& page,
                                                 std::vector<document::LayoutBlock>& blocks);
LayoutRecoveryStats recoverUnassignedTextLines(const document::PageText& text,
                                               const document::PageArtifact& page,
                                               document::PageLayout& layout);

} // namespace doc_parser::layout::detail
