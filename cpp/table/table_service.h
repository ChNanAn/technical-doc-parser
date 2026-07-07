#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/table_model.h"
#include "document/text_model.h"
#include "table/table_backend.h"

#include <memory>

namespace doc_parser::table {

class TableService {
public:
    explicit TableService(const ITableBackend& backend);
    explicit TableService(std::unique_ptr<ITableBackend> backend);

    bool recognize(const document::PageArtifact& page,
                   const document::PageText& text,
                   const document::PageLayout& layout,
                   document::PageTables& tables) const;

private:
    std::unique_ptr<ITableBackend> owned_backend_;
    const ITableBackend* backend_ = nullptr;
};

} // namespace doc_parser::table
