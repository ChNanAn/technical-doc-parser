#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/table_model.h"
#include "document/text_model.h"

namespace doc_parser::table {

struct TableRequest {
    const document::PageArtifact& page;
    const document::PageText& text;
    const document::PageLayout& layout;
};

struct TableResult {
    document::PageTables tables;
};

class ITableBackend {
public:
    virtual ~ITableBackend() = default;

    virtual bool recognize(const TableRequest& request, TableResult& result) const = 0;
};

class TextTableStructureBackend final : public ITableBackend {
public:
    bool recognize(const TableRequest& request, TableResult& result) const override;
};

} // namespace doc_parser::table
