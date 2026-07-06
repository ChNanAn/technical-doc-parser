#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/table_model.h"
#include "document/text_model.h"
#include "table/table_backend.h"

namespace doc_parser::table {

class TableService {
public:
    TableService();
    explicit TableService(const ITableBackend& backend);

    bool recognize(const document::PageArtifact& page,
                   const document::PageText& text,
                   const document::PageLayout& layout,
                   document::PageTables& tables) const;

private:
    const ITableBackend* backend_ = nullptr;
};

} // namespace doc_parser::table
