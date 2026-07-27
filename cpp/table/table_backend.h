#pragma once

#include "common/diagnostic.h"

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/table_model.h"
#include "document/text_model.h"

#include <document_intelligence_engine/engine_config.h>

#include <memory>
#include <vector>

namespace doc_parser::table {

struct TableRequest {
    const document::PageArtifact& page;
    const document::PageText& text;
    const document::PageLayout& layout;
};

struct TableResult {
    document::PageTables tables;
    std::vector<common::Diagnostic> diagnostics;
};

class ITableBackend {
public:
    virtual ~ITableBackend() = default;

    virtual bool recognize(const TableRequest& request, TableResult& result) const = 0;
    virtual bool isAvailable() const { return true; }
    virtual std::string unavailableReason() const { return {}; }
};

class TextTableStructureBackend final : public ITableBackend {
public:
    bool recognize(const TableRequest& request, TableResult& result) const override;
};

class TableTransformerOnnxBackend final : public ITableBackend {
public:
    TableTransformerOnnxBackend();
    explicit TableTransformerOnnxBackend(TableTransformerOnnxConfig config);
    ~TableTransformerOnnxBackend() override;

    TableTransformerOnnxBackend(const TableTransformerOnnxBackend&) = delete;
    TableTransformerOnnxBackend& operator=(const TableTransformerOnnxBackend&) = delete;
    TableTransformerOnnxBackend(TableTransformerOnnxBackend&&) noexcept;
    TableTransformerOnnxBackend& operator=(TableTransformerOnnxBackend&&) noexcept;

    bool isAvailable() const override;
    std::string unavailableReason() const override;
    const TableTransformerOnnxConfig& config() const;
    bool recognize(const TableRequest& request, TableResult& result) const override;

private:
    struct ModelBundle;
    static std::unique_ptr<ModelBundle> loadModels(const TableTransformerOnnxConfig& config);

    TableTransformerOnnxConfig config_;
    std::unique_ptr<ModelBundle> models_;
};

} // namespace doc_parser::table
