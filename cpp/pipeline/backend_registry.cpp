#include "pipeline/backend_registry.h"

#if DOC_PARSER_ENABLE_ONNXRUNTIME
#include "ocr/paddle_ocr_onnx_backend.h"
#endif
#include "ocr/tesseract_cli_ocr_backend.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <utility>

namespace doc_parser::pipeline {
namespace {

template <typename Backend, typename... Args> std::unique_ptr<Backend> makeAvailableBackend(Args&&... args) {
    auto backend = std::make_unique<Backend>(std::forward<Args>(args)...);
    if (!backend->isAvailable()) {
        return nullptr;
    }
    return backend;
}

bool validOrder(const std::vector<std::string>& order,
                const std::function<bool(const std::string&)>& registered,
                const std::string& stage,
                std::string& error) {
    if (order.empty()) {
        error = "auto_order." + stage + " must contain at least one backend";
        return false;
    }
    std::set<std::string> seen;
    for (const std::string& name : order) {
        if (name.empty() || name == "auto") {
            error = "auto_order." + stage + " contains an invalid backend name";
            return false;
        }
        if (!seen.insert(name).second) {
            error = "auto_order." + stage + " contains duplicate backend: " + name;
            return false;
        }
        if (!registered(name)) {
            error = "auto_order." + stage + " references an unregistered backend: " + name;
            return false;
        }
    }
    return true;
}

bool readOrder(const nlohmann::json& auto_order, const char* key, std::vector<std::string>& target, std::string& error) {
    if (!auto_order.contains(key)) {
        return true;
    }
    const nlohmann::json& value = auto_order.at(key);
    if (!value.is_array()) {
        error = std::string("auto_order.") + key + " must be an array";
        return false;
    }
    try {
        target = value.get<std::vector<std::string>>();
    } catch (const nlohmann::json::exception&) {
        error = std::string("auto_order.") + key + " must contain only strings";
        return false;
    }
    return true;
}

} // namespace

bool BackendRegistry::registerDocument(std::string name, DocumentFactory factory) {
    if (name.empty() || name == "auto" || !factory) {
        return false;
    }
    return document_factories_.emplace(std::move(name), std::move(factory)).second;
}

bool BackendRegistry::registerOcr(std::string name, TypedBackendRegistry<ocr::IOcrBackend>::Factory factory) {
    return ocr_.registerBackend(std::move(name), std::move(factory));
}

bool BackendRegistry::registerLayout(std::string name, TypedBackendRegistry<layout::ILayoutBackend>::Factory factory) {
    return layout_.registerBackend(std::move(name), std::move(factory));
}

bool BackendRegistry::registerTable(std::string name, TypedBackendRegistry<table::ITableBackend>::Factory factory) {
    return table_.registerBackend(std::move(name), std::move(factory));
}

bool BackendRegistry::hasDocument(const std::string& name) const {
    return document_factories_.find(name) != document_factories_.end();
}

bool BackendRegistry::hasOcr(const std::string& name) const { return ocr_.contains(name); }

bool BackendRegistry::hasLayout(const std::string& name) const { return layout_.contains(name); }

bool BackendRegistry::hasTable(const std::string& name) const { return table_.contains(name); }

std::vector<std::string> BackendRegistry::documentNames() const {
    std::vector<std::string> result;
    result.reserve(document_factories_.size());
    for (const auto& [name, factory] : document_factories_) {
        (void)factory;
        result.push_back(name);
    }
    return result;
}

std::vector<std::string> BackendRegistry::ocrNames() const { return ocr_.names(); }

std::vector<std::string> BackendRegistry::layoutNames() const { return layout_.names(); }

std::vector<std::string> BackendRegistry::tableNames() const { return table_.names(); }

DocumentBackendCreationResult BackendRegistry::createDocument(const std::string& name) const {
    const auto factory = document_factories_.find(name);
    if (factory == document_factories_.end()) {
        return {BackendCreationStatus::Unknown, {}};
    }
    document_source::DocumentSourceBundle backend = factory->second();
    if (backend.source == nullptr) {
        return {BackendCreationStatus::Unavailable, {}};
    }
    return {BackendCreationStatus::Created, std::move(backend)};
}

BackendCreationResult<ocr::IOcrBackend> BackendRegistry::createOcr(const std::string& name) const {
    return ocr_.create(name);
}

BackendCreationResult<layout::ILayoutBackend> BackendRegistry::createLayout(const std::string& name) const {
    return layout_.create(name);
}

BackendCreationResult<table::ITableBackend> BackendRegistry::createTable(const std::string& name) const {
    return table_.create(name);
}

BackendRegistry createDefaultBackendRegistry() {
    BackendRegistry registry;
    registry.registerDocument("pdf", [] { return document_source::createDocumentSource("pdf"); });
    registry.registerOcr("noop", [] { return std::make_unique<ocr::NoopOcrBackend>(); });
    registry.registerOcr("tesseract", [] { return makeAvailableBackend<ocr::TesseractCliOcrBackend>(); });
    registry.registerLayout("text", [] { return std::make_unique<layout::TextLayoutModelBackend>(); });
    registry.registerTable("text", [] { return std::make_unique<table::TextTableStructureBackend>(); });
#if DOC_PARSER_ENABLE_ONNXRUNTIME
    registry.registerOcr("paddle", [] { return makeAvailableBackend<ocr::PaddleOcrOnnxBackend>(); });
    registry.registerLayout("doclaynet", [] { return makeAvailableBackend<layout::DocLayNetOnnxBackend>(); });
    registry.registerLayout("paddle-layout", [] { return makeAvailableBackend<layout::PaddleDocLayoutOnnxBackend>(); });
    registry.registerTable("table-transformer",
                           [] { return makeAvailableBackend<table::TableTransformerOnnxBackend>(); });
#else
    registry.registerOcr("paddle", [] { return std::unique_ptr<ocr::IOcrBackend>{}; });
    registry.registerLayout("doclaynet", [] { return std::unique_ptr<layout::ILayoutBackend>{}; });
    registry.registerLayout("paddle-layout", [] { return std::unique_ptr<layout::ILayoutBackend>{}; });
    registry.registerTable("table-transformer", [] { return std::unique_ptr<table::ITableBackend>{}; });
#endif
    return registry;
}

BackendRegistryConfigResult loadBackendRegistryConfig(const std::filesystem::path& path,
                                                      const BackendRegistry& registry) {
    BackendRegistryConfigResult result;
    result.config = {};
    if (!path.empty()) {
        std::ifstream input(path);
        if (!input) {
            result.error = "failed to read backend registry config: " + path.string();
            return result;
        }
        try {
            const nlohmann::json document = nlohmann::json::parse(input);
            if (!document.is_object()) {
                result.error = "backend registry config root must be an object";
                return result;
            }
            static const std::set<std::string> kRootKeys{"version", "auto_order"};
            for (const auto& [key, value] : document.items()) {
                (void)value;
                if (kRootKeys.find(key) == kRootKeys.end()) {
                    result.error = "unknown backend registry config field: " + key;
                    return result;
                }
            }
            result.config.version = document.value("version", 1);
            if (result.config.version != 1) {
                result.error = "unsupported backend registry config version: " + std::to_string(result.config.version);
                return result;
            }
            if (document.contains("auto_order")) {
                const nlohmann::json& auto_order = document.at("auto_order");
                if (!auto_order.is_object()) {
                    result.error = "auto_order must be an object";
                    return result;
                }
                static const std::set<std::string> kStages{"document", "ocr", "layout", "table"};
                for (const auto& [key, value] : auto_order.items()) {
                    (void)value;
                    if (kStages.find(key) == kStages.end()) {
                        result.error = "unknown auto_order stage: " + key;
                        return result;
                    }
                }
                if (!readOrder(auto_order, "document", result.config.document_auto_order, result.error) ||
                    !readOrder(auto_order, "ocr", result.config.ocr_auto_order, result.error) ||
                    !readOrder(auto_order, "layout", result.config.layout_auto_order, result.error) ||
                    !readOrder(auto_order, "table", result.config.table_auto_order, result.error)) {
                    return result;
                }
            }
        } catch (const nlohmann::json::exception& error) {
            result.error = "invalid backend registry config: " + std::string(error.what());
            return result;
        }
    }

    if (!validOrder(
            result.config.document_auto_order,
            [&](const std::string& name) { return registry.hasDocument(name); },
            "document",
            result.error) ||
        !validOrder(
            result.config.ocr_auto_order,
            [&](const std::string& name) { return registry.hasOcr(name); },
            "ocr",
            result.error) ||
        !validOrder(
            result.config.layout_auto_order,
            [&](const std::string& name) { return registry.hasLayout(name); },
            "layout",
            result.error) ||
        !validOrder(
            result.config.table_auto_order,
            [&](const std::string& name) { return registry.hasTable(name); },
            "table",
            result.error)) {
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace doc_parser::pipeline
