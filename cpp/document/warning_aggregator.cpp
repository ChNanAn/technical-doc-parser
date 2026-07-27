#include "document/warning_aggregator.h"

#include <algorithm>
#include <limits>
#include <map>
#include <tuple>

namespace doc_parser::document {
namespace {

using WarningIdentity =
    std::tuple<std::string, std::string, std::string, std::string, std::map<std::string, std::string>>;

WarningIdentity identity(const DocumentWarning& warning) {
    return {
        warning.code,
        warning.message,
        warning.stage,
        warning.block_id,
        warning.details,
    };
}

void appendPage(std::vector<std::string>& page_ids, const std::string& page_id) {
    if (!page_id.empty() && std::find(page_ids.begin(), page_ids.end(), page_id) == page_ids.end()) {
        page_ids.push_back(page_id);
    }
}

void appendPages(std::vector<std::string>& page_ids, const DocumentWarning& warning) {
    appendPage(page_ids, warning.page_id);
    for (const std::string& page_id : warning.page_ids) {
        appendPage(page_ids, page_id);
    }
}

std::size_t saturatedAdd(std::size_t left, std::size_t right) {
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    return right > maximum - left ? maximum : left + right;
}

} // namespace

std::vector<DocumentWarning> aggregateWarnings(const std::vector<DocumentWarning>& warnings) {
    std::vector<DocumentWarning> aggregated;
    std::map<WarningIdentity, std::size_t> indices;
    for (const DocumentWarning& warning : warnings) {
        const WarningIdentity key = identity(warning);
        const auto existing = indices.find(key);
        if (existing == indices.end()) {
            DocumentWarning value = warning;
            value.page_id.clear();
            value.page_ids.clear();
            appendPages(value.page_ids, warning);
            indices.emplace(key, aggregated.size());
            aggregated.push_back(std::move(value));
            continue;
        }

        DocumentWarning& value = aggregated[existing->second];
        value.occurrence_count = saturatedAdd(value.occurrence_count, warning.occurrence_count);
        appendPages(value.page_ids, warning);
    }

    for (DocumentWarning& warning : aggregated) {
        if (warning.page_ids.size() == 1U) {
            warning.page_id = warning.page_ids.front();
            warning.page_ids.clear();
        }
    }
    return aggregated;
}

} // namespace doc_parser::document
