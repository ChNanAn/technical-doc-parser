#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/text_model.h"

#include <vector>

namespace doc_parser::layout::detail {

void assignTextLines(const document::PageText& text, std::vector<document::LayoutBlock>& blocks);
void associateCaptions(const document::PageArtifact& page, std::vector<document::LayoutBlock>& blocks);

} // namespace doc_parser::layout::detail
