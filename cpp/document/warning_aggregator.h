#pragma once

#include "document/parsed_document.h"

#include <vector>

namespace doc_parser::document {

std::vector<DocumentWarning> aggregateWarnings(const std::vector<DocumentWarning>& warnings);

} // namespace doc_parser::document
