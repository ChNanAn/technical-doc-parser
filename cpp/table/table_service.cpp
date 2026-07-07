#include "table/table_service.h"

#include <utility>

namespace doc_parser::table {

TableService::TableService(const ITableBackend& backend) : backend_(&backend) {}

TableService::TableService(std::unique_ptr<ITableBackend> backend)
    : owned_backend_(std::move(backend)), backend_(owned_backend_.get()) {}

bool TableService::recognize(const document::PageArtifact& page,
                             const document::PageText& text,
                             const document::PageLayout& layout,
                             document::PageTables& tables) const {
    tables = {};
    tables.page_index = page.page_index;
    tables.page_number = page.page_number;

    if (backend_ == nullptr) {
        return false;
    }

    TableResult result;
    if (!backend_->recognize({page, text, layout}, result)) {
        return false;
    }

    tables = result.tables;
    return true;
}

} // namespace doc_parser::table
