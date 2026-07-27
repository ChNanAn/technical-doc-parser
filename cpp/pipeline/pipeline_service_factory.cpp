#include "pipeline/pipeline_service_factory.h"

#include "common/warning_codes.h"
#include "reading_order/reading_order_backend.h"

#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>
#include <vector>

namespace doc_parser::pipeline {
namespace {

#ifndef DOC_PARSER_ENGINE_VERSION
#define DOC_PARSER_ENGINE_VERSION "unknown"
#endif

#ifndef DOC_PARSER_GIT_REVISION
#define DOC_PARSER_GIT_REVISION ""
#endif

template <typename Interface> struct NamedBackend {
    std::string name;
    std::unique_ptr<Interface> backend;
};

class ChainedLayoutBackend final : public layout::ILayoutBackend {
public:
    explicit ChainedLayoutBackend(std::vector<NamedBackend<layout::ILayoutBackend>> backends)
        : backends_(std::move(backends)) {}

    bool analyze(const layout::LayoutRequest& request, layout::LayoutResult& result) const override {
        std::vector<common::Diagnostic> diagnostics;
        for (std::size_t index = 0; index < backends_.size(); ++index) {
            layout::LayoutResult candidate;
            if (backends_[index].backend->analyze(request, candidate)) {
                candidate.diagnostics.insert(candidate.diagnostics.begin(), diagnostics.begin(), diagnostics.end());
                result = std::move(candidate);
                return true;
            }
            if (index + 1 < backends_.size()) {
                spdlog::warn("layout: {} inference failed for page {}; falling back to {}",
                             backends_[index].name,
                             request.page.page_number,
                             backends_[index + 1].name);
                diagnostics.push_back({
                    common::warning_codes::kLayoutBackendFallback,
                    "layout inference failed; continued with the configured fallback chain",
                    "layout",
                    request.page.page_number,
                    {
                        {"failed_backend", backends_[index].name},
                        {"fallback_backend", backends_[index + 1].name},
                        {"reason", "inference_failed"},
                    },
                });
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
        std::vector<common::Diagnostic> diagnostics;
        for (std::size_t index = 0; index < backends_.size(); ++index) {
            table::TableResult candidate;
            if (backends_[index].backend->recognize(request, candidate)) {
                candidate.diagnostics.insert(candidate.diagnostics.begin(), diagnostics.begin(), diagnostics.end());
                result = std::move(candidate);
                return true;
            }
            if (index + 1 < backends_.size()) {
                spdlog::warn("table: {} inference failed for page {}; falling back to {}",
                             backends_[index].name,
                             request.page.page_number,
                             backends_[index + 1].name);
                diagnostics.push_back({
                    common::warning_codes::kTableBackendFallback,
                    "table inference failed; continued with the configured fallback chain",
                    "table",
                    request.page.page_number,
                    {
                        {"failed_backend", backends_[index].name},
                        {"fallback_backend", backends_[index + 1].name},
                        {"reason", "inference_failed"},
                    },
                });
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

std::string joinDetails(const std::vector<std::string>& details) {
    std::string joined;
    for (const std::string& detail : details) {
        joined += (joined.empty() ? "" : "; ") + detail;
    }
    return joined;
}

void setCreationError(PipelineServiceCreationResult& result,
                      const std::string& stage,
                      const std::string& backend,
                      BackendCreationStatus status,
                      const std::string& detail = {}) {
    std::string reason = status == BackendCreationStatus::Unknown ? "unknown " + stage + " backend: " + backend
                                                                  : stage + " backend is unavailable: " + backend;
    if (!detail.empty()) {
        reason += ": " + detail;
    }
    const std::string code = status == BackendCreationStatus::Unknown ? "configure.backend_unknown"
                                                                      : "configure.backend_unavailable";
    result.status = common::Status::error(code, reason, "configure");
    spdlog::error("{}: {}", result.status.code(), result.status.message());
}

bool containsBackend(const std::string& resolved, const std::string& backend) {
    std::size_t offset = 0;
    while (offset <= resolved.size()) {
        const std::size_t separator = resolved.find("->", offset);
        const std::size_t length = separator == std::string::npos ? resolved.size() - offset : separator - offset;
        if (resolved.compare(offset, length, backend) == 0) {
            return true;
        }
        if (separator == std::string::npos) {
            break;
        }
        offset = separator + 2;
    }
    return false;
}

void addModel(RunProvenance& provenance,
              std::string stage,
              std::string backend,
              std::string role,
              const std::filesystem::path& path,
              std::string profile = {}) {
    if (!path.empty()) {
        provenance.models.push_back({std::move(stage), std::move(backend), std::move(role), path, std::move(profile)});
    }
}

void addConfiguredModels(const EngineConfig& config, RunProvenance& provenance) {
    const BackendOptions& resolved = provenance.backends.resolved;
    if (resolved.ocr == "paddle") {
        addModel(provenance,
                 "text",
                 "paddle",
                 "detection",
                 config.paddle_ocr.detection_model,
                 config.paddle_ocr.profile.name);
        addModel(provenance,
                 "text",
                 "paddle",
                 "recognition",
                 config.paddle_ocr.recognition_model,
                 config.paddle_ocr.profile.name);
        addModel(provenance,
                 "text",
                 "paddle",
                 "character_dictionary",
                 config.paddle_ocr.character_dict,
                 config.paddle_ocr.profile.name);
    } else if (resolved.ocr == "tesseract") {
        addModel(provenance, "text", "tesseract", "executable", config.tesseract.executable, config.tesseract.language);
    }
    if (containsBackend(resolved.layout, "doclaynet")) {
        addModel(provenance, "layout", "doclaynet", "layout", config.doclaynet.model_path);
    }
    if (containsBackend(resolved.layout, "paddle-layout")) {
        addModel(provenance, "layout", "paddle-layout", "layout", config.paddle_layout.model_path);
    }
    if (containsBackend(resolved.table, "table-transformer")) {
        addModel(provenance, "table", "table-transformer", "detection", config.table_transformer.detection_model_path);
        addModel(provenance, "table", "table-transformer", "structure", config.table_transformer.structure_model_path);
    }
}

} // namespace

PipelineServiceCreationResult createPipelineServices(const EngineConfig& config) {
    const BackendRegistry registry = createDefaultBackendRegistry(config);
    return createPipelineServices(config, registry);
}

PipelineServiceCreationResult createPipelineServices(const EngineConfig& config, const BackendRegistry& registry) {
    PipelineServiceCreationResult result = createPipelineServices(config.backends, registry);
    if (result.status.okStatus()) {
        addConfiguredModels(config, result.provenance);
    }
    return result;
}

PipelineServiceCreationResult createPipelineServices(const BackendOptions& options, const BackendRegistry& registry) {
    PipelineServiceCreationResult result;
    result.provenance.engine_version = DOC_PARSER_ENGINE_VERSION;
    result.provenance.git_revision = DOC_PARSER_GIT_REVISION;
    result.provenance.backends.requested = options;
    const BackendRegistryConfigResult config_result = loadBackendRegistryConfig(options.registry_config, registry);
    if (!config_result.ok) {
        result.status = common::Status::error("configure.backend_registry_invalid", config_result.error, "configure");
        spdlog::error("{}: {}", result.status.code(), result.status.message());
        return result;
    }
    const BackendRegistryConfig& config = config_result.config;
    const std::string config_source = options.registry_config.empty() ? "builtin" : options.registry_config.string();
    result.provenance.backends.config_source = config_source;
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
            result.status = common::Status::error("configure.document_backend_unavailable",
                                                  "no document backend from configured auto_order is available",
                                                  "configure");
            spdlog::error("{}: {}", result.status.code(), result.status.message());
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
        std::vector<std::string> unavailable_reasons;
        for (const std::string& name : config.ocr_auto_order) {
            BackendCreationResult<ocr::IOcrBackend> candidate = registry.createOcr(name);
            if (candidate.status == BackendCreationStatus::Created) {
                selected_ocr = name;
                ocr_backend = std::move(candidate.backend);
                break;
            }
            unavailable_reasons.push_back(name + ": " + candidate.error_message);
            spdlog::debug("backend registry: skipping unavailable OCR backend '{}'", name);
        }
        if (ocr_backend == nullptr) {
            selected_ocr = "unavailable";
            spdlog::warn("configure_ocr_backend: auto order found no usable OCR backend; native-text documents can "
                         "still be processed, but pages requiring OCR will fail");
            ocr_backend = std::make_unique<ocr::UnavailableOcrBackend>("no OCR backend from configured auto_order is "
                                                                       "available (" +
                                                                       joinDetails(unavailable_reasons) +
                                                                       "); install a backend or select --ocr-backend "
                                                                       "noop explicitly");
        }
    } else {
        BackendCreationResult<ocr::IOcrBackend> creation = registry.createOcr(options.ocr);
        if (creation.status != BackendCreationStatus::Created) {
            setCreationError(result, "ocr", options.ocr, creation.status, creation.error_message);
            return result;
        }
        ocr_backend = std::move(creation.backend);
    }

    std::unique_ptr<layout::ILayoutBackend> layout_backend;
    std::string selected_layout = options.layout;
    if (options.layout == "auto") {
        std::vector<NamedBackend<layout::ILayoutBackend>> available;
        std::vector<std::string> selected_names;
        std::vector<std::string> unavailable_reasons;
        for (const std::string& name : config.layout_auto_order) {
            BackendCreationResult<layout::ILayoutBackend> candidate = registry.createLayout(name);
            if (candidate.status == BackendCreationStatus::Created) {
                selected_names.push_back(name);
                available.push_back({name, std::move(candidate.backend)});
            } else {
                unavailable_reasons.push_back(name + ": " + candidate.error_message);
                spdlog::debug("backend registry: skipping unavailable layout backend '{}'", name);
            }
        }
        if (available.empty()) {
            result.status = common::Status::error(
                "configure.layout_backend_unavailable",
                "no layout backend from configured auto_order is available: " + joinDetails(unavailable_reasons),
                "configure");
            spdlog::error("{}: {}", result.status.code(), result.status.message());
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
            setCreationError(result, "layout", options.layout, creation.status, creation.error_message);
            return result;
        }
        layout_backend = std::move(creation.backend);
    }

    std::unique_ptr<table::ITableBackend> table_backend;
    std::string selected_table = options.table;
    if (options.table == "auto") {
        std::vector<NamedBackend<table::ITableBackend>> available;
        std::vector<std::string> selected_names;
        std::vector<std::string> unavailable_reasons;
        for (const std::string& name : config.table_auto_order) {
            BackendCreationResult<table::ITableBackend> candidate = registry.createTable(name);
            if (candidate.status == BackendCreationStatus::Created) {
                selected_names.push_back(name);
                available.push_back({name, std::move(candidate.backend)});
            } else {
                unavailable_reasons.push_back(name + ": " + candidate.error_message);
                spdlog::debug("backend registry: skipping unavailable table backend '{}'", name);
            }
        }
        if (available.empty()) {
            result.status = common::Status::error(
                "configure.table_backend_unavailable",
                "no table backend from configured auto_order is available: " + joinDetails(unavailable_reasons),
                "configure");
            spdlog::error("{}: {}", result.status.code(), result.status.message());
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
            setCreationError(result, "table", options.table, creation.status, creation.error_message);
            return result;
        }
        table_backend = std::move(creation.backend);
    }

    result.services.ocr = std::move(ocr_backend);
    result.services.layout = std::move(layout_backend);
    result.services.reading_order = std::make_unique<reading_order::DoclingLikeReadingOrderBackend>();
    result.services.table = std::move(table_backend);
    result.provenance.backends.resolved = {
        selected_document,
        selected_ocr,
        selected_layout,
        selected_table,
        options.registry_config,
    };
    result.trace_message = "registry=" + config_source + ", document=" + selected_document + ", ocr=" + selected_ocr +
                           ", layout=" + selected_layout + ", table=" + selected_table;
    result.status = common::Status::ok();
    return result;
}

} // namespace doc_parser::pipeline
