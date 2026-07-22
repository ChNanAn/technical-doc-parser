#pragma once

#include "document/layout_model.h"
#include "document/page_artifact.h"
#include "document/table_model.h"
#include "document/text_model.h"

#include <filesystem>
#include <memory>

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

struct TableTransformerOnnxConfig {
    std::filesystem::path detection_model_path;
    std::filesystem::path structure_model_path;
    double detection_confidence_threshold = 0.9;
    double structure_confidence_threshold = 0.5;
    int crop_padding = 20;
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

    bool isAvailable() const;
    const TableTransformerOnnxConfig& config() const;
    bool recognize(const TableRequest& request, TableResult& result) const override;

private:
    struct ModelBundle;
    static std::unique_ptr<ModelBundle> loadModels(const TableTransformerOnnxConfig& config);

    TableTransformerOnnxConfig config_;
    std::unique_ptr<ModelBundle> models_;
};

} // namespace doc_parser::table
