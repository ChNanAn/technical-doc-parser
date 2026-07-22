#pragma once

#include "document_source/document_source_factory.h"
#include "layout/layout_backend.h"
#include "ocr/ocr_backend.h"
#include "table/table_backend.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace doc_parser::pipeline {

enum class BackendCreationStatus {
    Created,
    Unknown,
    Unavailable,
};

template <typename Interface> struct BackendCreationResult {
    BackendCreationStatus status = BackendCreationStatus::Unknown;
    std::unique_ptr<Interface> backend;
};

template <typename Interface> class TypedBackendRegistry {
public:
    using Factory = std::function<std::unique_ptr<Interface>()>;

    bool registerBackend(std::string name, Factory factory) {
        if (name.empty() || name == "auto" || !factory) {
            return false;
        }
        return factories_.emplace(std::move(name), std::move(factory)).second;
    }

    bool contains(const std::string& name) const { return factories_.find(name) != factories_.end(); }

    BackendCreationResult<Interface> create(const std::string& name) const {
        const auto factory = factories_.find(name);
        if (factory == factories_.end()) {
            return {BackendCreationStatus::Unknown, nullptr};
        }
        std::unique_ptr<Interface> backend = factory->second();
        if (backend == nullptr) {
            return {BackendCreationStatus::Unavailable, nullptr};
        }
        return {BackendCreationStatus::Created, std::move(backend)};
    }

    std::vector<std::string> names() const {
        std::vector<std::string> result;
        result.reserve(factories_.size());
        for (const auto& [name, factory] : factories_) {
            (void)factory;
            result.push_back(name);
        }
        return result;
    }

private:
    std::map<std::string, Factory> factories_;
};

struct DocumentBackendCreationResult {
    BackendCreationStatus status = BackendCreationStatus::Unknown;
    document_source::DocumentSourceBundle backend;
};

class BackendRegistry {
public:
    using DocumentFactory = std::function<document_source::DocumentSourceBundle()>;

    bool registerDocument(std::string name, DocumentFactory factory);
    bool registerOcr(std::string name, TypedBackendRegistry<ocr::IOcrBackend>::Factory factory);
    bool registerLayout(std::string name, TypedBackendRegistry<layout::ILayoutBackend>::Factory factory);
    bool registerTable(std::string name, TypedBackendRegistry<table::ITableBackend>::Factory factory);

    bool hasDocument(const std::string& name) const;
    bool hasOcr(const std::string& name) const;
    bool hasLayout(const std::string& name) const;
    bool hasTable(const std::string& name) const;

    DocumentBackendCreationResult createDocument(const std::string& name) const;
    BackendCreationResult<ocr::IOcrBackend> createOcr(const std::string& name) const;
    BackendCreationResult<layout::ILayoutBackend> createLayout(const std::string& name) const;
    BackendCreationResult<table::ITableBackend> createTable(const std::string& name) const;

private:
    std::map<std::string, DocumentFactory> document_factories_;
    TypedBackendRegistry<ocr::IOcrBackend> ocr_;
    TypedBackendRegistry<layout::ILayoutBackend> layout_;
    TypedBackendRegistry<table::ITableBackend> table_;
};

struct BackendRegistryConfig {
    int version = 1;
    std::vector<std::string> document_auto_order{"pdf"};
    std::vector<std::string> ocr_auto_order{"paddle", "tesseract"};
    std::vector<std::string> layout_auto_order{"doclaynet", "paddle-layout", "text"};
    std::vector<std::string> table_auto_order{"table-transformer", "text"};
};

struct BackendRegistryConfigResult {
    bool ok = false;
    BackendRegistryConfig config;
    std::string error;
};

BackendRegistry createDefaultBackendRegistry();
BackendRegistryConfigResult loadBackendRegistryConfig(const std::filesystem::path& path,
                                                      const BackendRegistry& registry);

} // namespace doc_parser::pipeline
