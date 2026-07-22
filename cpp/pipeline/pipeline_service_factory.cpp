#include "pipeline/pipeline_service_factory.h"

#include "reading_order/reading_order_backend.h"

#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>
#include <vector>

namespace doc_parser::pipeline {
namespace {

template <typename Interface> struct NamedBackend {
    std::string name;
    std::unique_ptr<Interface> backend;
};

class ChainedLayoutBackend final : public layout::ILayoutBackend {
public:
    explicit ChainedLayoutBackend(std::vector<NamedBackend<layout::ILayoutBackend>> backends)
        : backends_(std::move(backends)) {}

    bool analyze(const layout::LayoutRequest& request, layout::LayoutResult& result) const override {
        for (std::size_t index = 0; index < backends_.size(); ++index) {
            if (backends_[index].backend->analyze(request, result)) {
                return true;
            }
            if (index + 1 < backends_.size()) {
                spdlog::warn("layout: {} inference failed for page {}; falling back to {}",
                             backends_[index].name,
                             request.page.page_number,
                             backends_[index + 1].name);
            }
        }
        return false;
    }

private:
    std::vector<NamedBackend<layout::ILayoutBackend>> backends_;
};

class ChainedTableBackend final : public table::ITableBackend {
public:
    explicit ChainedTableBackend(std::vector<NamedBackend<table::ITableBackend>> backends)
        : backends_(std::move(backends)) {}

    bool recognize(const table::TableRequest& request, table::TableResult& result) const override {
        for (std::size_t index = 0; index < backends_.size(); ++index) {
            if (backends_[index].backend->recognize(request, result)) {
                return true;
            }
            if (index + 1 < backends_.size()) {
                spdlog::warn("table: {} inference failed for page {}; falling back to {}",
                             backends_[index].name,
                             request.page.page_number,
                             backends_[index + 1].name);
            }
        }
        return false;
    }

private:
    std::vector<NamedBackend<table::ITableBackend>> backends_;
};

std::string joinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (const std::string& name : names) {
        joined += (joined.empty() ? "" : "->") + name;
    }
    return joined;
}

void setCreationError(PipelineServiceCreationResult& result,
                      const std::string& stage,
                      const std::string& backend,
                      BackendCreationStatus status) {
    result.error_stage = "configure_" + stage + "_backend";
    result.error_message = status == BackendCreationStatus::Unknown ? "unknown " + stage + " backend: " + backend
                                                                    : stage + " backend is unavailable: " + backend;
    spdlog::error("{}: {}", result.error_stage, result.error_message);
}

} // namespace

PipelineServiceCreationResult createPipelineServices(const BackendOptions& options) {
    const BackendRegistry registry = createDefaultBackendRegistry();
    return createPipelineServices(options, registry);
}

PipelineServiceCreationResult createPipelineServices(const BackendOptions& options, const BackendRegistry& registry) {
    PipelineServiceCreationResult result;
    const BackendRegistryConfigResult config_result = loadBackendRegistryConfig(options.registry_config, registry);
    if (!config_result.ok) {
        result.error_stage = "configure_backend_registry";
        result.error_message = config_result.error;
        spdlog::error("configure_backend_registry: {}", result.error_message);
        return result;
    }
    const BackendRegistryConfig& config = config_result.config;
    const std::string config_source = options.registry_config.empty() ? "builtin" : options.registry_config.string();
    spdlog::debug("backend registry: config={} document_auto={} ocr_auto={} layout_auto={} table_auto={}",
                  config_source,
                  joinNames(config.document_auto_order),
                  joinNames(config.ocr_auto_order),
                  joinNames(config.layout_auto_order),
                  joinNames(config.table_auto_order));

    std::string selected_document = options.document;
    DocumentBackendCreationResult document;
    if (options.document == "auto") {
        for (const std::string& name : config.document_auto_order) {
            DocumentBackendCreationResult candidate = registry.createDocument(name);
            if (candidate.status == BackendCreationStatus::Created) {
                selected_document = name;
                document = std::move(candidate);
                break;
            }
            spdlog::debug("backend registry: skipping unavailable document backend '{}'", name);
        }
        if (document.status != BackendCreationStatus::Created) {
            result.error_stage = "configure_document_source_backend";
            result.error_message = "no document backend from configured auto_order is available";
            spdlog::error("{}: {}", result.error_stage, result.error_message);
            return result;
        }
    } else {
        document = registry.createDocument(options.document);
        if (document.status != BackendCreationStatus::Created) {
            setCreationError(result, "document_source", options.document, document.status);
            return result;
        }
    }
    result.services.document = std::move(document.backend);

    std::unique_ptr<ocr::IOcrBackend> ocr_backend;
    std::string selected_ocr = options.ocr;
    if (options.ocr == "auto") {
        for (const std::string& name : config.ocr_auto_order) {
            BackendCreationResult<ocr::IOcrBackend> candidate = registry.createOcr(name);
            if (candidate.status == BackendCreationStatus::Created) {
                selected_ocr = name;
                ocr_backend = std::move(candidate.backend);
                break;
            }
            spdlog::debug("backend registry: skipping unavailable OCR backend '{}'", name);
        }
        if (ocr_backend == nullptr) {
            selected_ocr = "unavailable";
            spdlog::warn("configure_ocr_backend: auto order found no usable OCR backend; native-text documents can "
                         "still be processed, but pages requiring OCR will fail");
            ocr_backend = std::make_unique<ocr::UnavailableOcrBackend>("no OCR backend from configured auto_order is "
                                                                       "available; install a backend or select "
                                                                       "--ocr-backend "
                                                                       "noop explicitly");
        }
    } else {
        BackendCreationResult<ocr::IOcrBackend> creation = registry.createOcr(options.ocr);
        if (creation.status != BackendCreationStatus::Created) {
            setCreationError(result, "ocr", options.ocr, creation.status);
            return result;
        }
        ocr_backend = std::move(creation.backend);
    }

    std::unique_ptr<layout::ILayoutBackend> layout_backend;
    std::string selected_layout = options.layout;
    if (options.layout == "auto") {
        std::vector<NamedBackend<layout::ILayoutBackend>> available;
        std::vector<std::string> selected_names;
        for (const std::string& name : config.layout_auto_order) {
            BackendCreationResult<layout::ILayoutBackend> candidate = registry.createLayout(name);
            if (candidate.status == BackendCreationStatus::Created) {
                selected_names.push_back(name);
                available.push_back({name, std::move(candidate.backend)});
            } else {
                spdlog::debug("backend registry: skipping unavailable layout backend '{}'", name);
            }
        }
        if (available.empty()) {
            result.error_stage = "configure_layout_backend";
            result.error_message = "no layout backend from configured auto_order is available";
            spdlog::error("{}: {}", result.error_stage, result.error_message);
            return result;
        }
        selected_layout = joinNames(selected_names);
        if (available.size() == 1) {
            layout_backend = std::move(available.front().backend);
        } else {
            layout_backend = std::make_unique<ChainedLayoutBackend>(std::move(available));
        }
    } else {
        BackendCreationResult<layout::ILayoutBackend> creation = registry.createLayout(options.layout);
        if (creation.status != BackendCreationStatus::Created) {
            setCreationError(result, "layout", options.layout, creation.status);
            return result;
        }
        layout_backend = std::move(creation.backend);
    }

    std::unique_ptr<table::ITableBackend> table_backend;
    std::string selected_table = options.table;
    if (options.table == "auto") {
        std::vector<NamedBackend<table::ITableBackend>> available;
        std::vector<std::string> selected_names;
        for (const std::string& name : config.table_auto_order) {
            BackendCreationResult<table::ITableBackend> candidate = registry.createTable(name);
            if (candidate.status == BackendCreationStatus::Created) {
                selected_names.push_back(name);
                available.push_back({name, std::move(candidate.backend)});
            } else {
                spdlog::debug("backend registry: skipping unavailable table backend '{}'", name);
            }
        }
        if (available.empty()) {
            result.error_stage = "configure_table_backend";
            result.error_message = "no table backend from configured auto_order is available";
            spdlog::error("{}: {}", result.error_stage, result.error_message);
            return result;
        }
        selected_table = joinNames(selected_names);
        if (available.size() == 1) {
            table_backend = std::move(available.front().backend);
        } else {
            table_backend = std::make_unique<ChainedTableBackend>(std::move(available));
        }
    } else {
        BackendCreationResult<table::ITableBackend> creation = registry.createTable(options.table);
        if (creation.status != BackendCreationStatus::Created) {
            setCreationError(result, "table", options.table, creation.status);
            return result;
        }
        table_backend = std::move(creation.backend);
    }

    result.services.ocr = std::move(ocr_backend);
    result.services.layout = std::move(layout_backend);
    result.services.reading_order = std::make_unique<reading_order::DoclingLikeReadingOrderBackend>();
    result.services.table = std::move(table_backend);
    result.trace_message = "registry=" + config_source + ", document=" + selected_document + ", ocr=" + selected_ocr +
                           ", layout=" + selected_layout + ", table=" + selected_table;
    result.ok = true;
    return result;
}

} // namespace doc_parser::pipeline
