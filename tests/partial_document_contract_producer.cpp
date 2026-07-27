#include "common/warning_codes.h"
#include "document_source/document_source_factory.h"
#include "export/document_exporter.h"
#include "layout/layout_backend.h"
#include "ocr/ocr_backend.h"
#include "pipeline/backend_registry.h"
#include "document_intelligence_engine/document_engine.h"
#include "table/table_backend.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace {

class FailingLayoutBackend final : public doc_parser::layout::ILayoutBackend {
public:
    bool analyze(const doc_parser::layout::LayoutRequest&, doc_parser::layout::LayoutResult&) const override {
        return false;
    }
};

bool configureRegistry(doc_parser::pipeline::BackendRegistry& registry) {
    return registry.registerDocument(
               "pdf", [] { return doc_parser::document_source::createDocumentSource("pdf"); }) &&
           registry.registerOcr(
               "noop", [] { return std::make_unique<doc_parser::ocr::NoopOcrBackend>(); }) &&
           registry.registerLayout(
               "failing-layout", [] { return std::make_unique<FailingLayoutBackend>(); }) &&
           registry.registerLayout(
               "text", [] { return std::make_unique<doc_parser::layout::TextLayoutModelBackend>(); }) &&
           registry.registerTable(
               "text", [] { return std::make_unique<doc_parser::table::TextTableStructureBackend>(); });
}

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        return fail("usage: partial_document_contract_producer INPUT_PDF OUTPUT_DIRECTORY");
    }

    const std::filesystem::path input_path = argv[1];
    const std::filesystem::path output_directory = argv[2];
    std::filesystem::create_directories(output_directory);
    const std::filesystem::path registry_config = output_directory / "backends.json";
    {
        std::ofstream output(registry_config);
        if (!output) {
            return fail("failed to create backend registry fixture");
        }
        output
            << R"({"version":1,"auto_order":{"document":["pdf"],"ocr":["noop"],"layout":["failing-layout","text"],"table":["text"]}})";
    }

    doc_parser::pipeline::BackendRegistry registry;
    if (!configureRegistry(registry)) {
        return fail("failed to configure contract-test backend registry");
    }

    doc_parser::pipeline::EngineConfig config = doc_parser::pipeline::defaultEngineConfig();
    config.backends.document = "pdf";
    config.backends.ocr = "noop";
    config.backends.layout = "auto";
    config.backends.table = "text";
    config.backends.registry_config = registry_config;
    doc_parser::pipeline::DocumentEngine engine(std::move(config), registry);
    if (!engine.isReady()) {
        return fail("engine initialization failed: " + engine.initializationStatus().message());
    }

    doc_parser::pipeline::DocumentParseOptions options;
    options.input_path = input_path;
    options.output_directory = output_directory;
    options.run_id = "run_partial_contract";
    options.render.dpi = 72;
    doc_parser::pipeline::ParseResult result = engine.parse(options);
    if (!result.ok()) {
        return fail("engine parse failed: " + result.status.message());
    }
    if (result.document.status != doc_parser::document::DocumentStatus::Partial || result.document.warnings.empty()) {
        return fail("runtime fallback did not produce an explained partial document");
    }
    if (result.document.warnings.size() != 1U) {
        return fail("equivalent runtime fallback warnings were not aggregated");
    }
    const doc_parser::document::DocumentWarning& warning = result.document.warnings.front();
    if (warning.code != doc_parser::common::warning_codes::kLayoutBackendFallback) {
        return fail("runtime fallback produced an unexpected warning code: " + warning.code);
    }
    if (warning.occurrence_count != 3U ||
        warning.page_ids != std::vector<std::string>({"page_1", "page_2", "page_3"})) {
        return fail("aggregated runtime fallback warning lost occurrence or page evidence");
    }

    const std::unique_ptr<doc_parser::exporter::IDocumentExporter> exporter =
        doc_parser::exporter::createDefaultDocumentExporter();
    if (exporter == nullptr) {
        return fail("default document exporter is unavailable");
    }
    const doc_parser::common::Status export_status =
        exporter->write({false, output_directory / "document.json", &result.document, &result.artifacts});
    if (!export_status.okStatus()) {
        return fail("document export failed: " + export_status.message());
    }
    return 0;
}
