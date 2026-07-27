#include "document_source/document_source_factory.h"
#include "export/document_exporter.h"
#include "layout/layout_backend.h"
#include "ocr/ocr_backend.h"
#include "pipeline/backend_registry.h"
#include "pipeline/document_engine.h"
#include "pipeline/document_pipeline.h"
#include "table/table_backend.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>

namespace {

struct FactoryCounts {
    int document = 0;
    int ocr = 0;
    int layout = 0;
    int table = 0;
};

} // namespace

TEST(DocumentEngineTest, ReusesBackendInstancesAndLeavesExportToCaller) {
    auto counts = std::make_shared<FactoryCounts>();
    doc_parser::pipeline::BackendRegistry registry;
    ASSERT_TRUE(registry.registerDocument("pdf", [counts] {
        ++counts->document;
        return doc_parser::document_source::createDocumentSource("pdf");
    }));
    ASSERT_TRUE(registry.registerOcr("noop", [counts] {
        ++counts->ocr;
        return std::make_unique<doc_parser::ocr::NoopOcrBackend>();
    }));
    ASSERT_TRUE(registry.registerLayout("text", [counts] {
        ++counts->layout;
        return std::make_unique<doc_parser::layout::TextLayoutModelBackend>();
    }));
    ASSERT_TRUE(registry.registerTable("text", [counts] {
        ++counts->table;
        return std::make_unique<doc_parser::table::TextTableStructureBackend>();
    }));

    doc_parser::pipeline::BackendOptions backend_options;
    backend_options.document = "pdf";
    backend_options.ocr = "noop";
    backend_options.layout = "text";
    backend_options.table = "text";
    const std::filesystem::path registry_config = std::filesystem::temp_directory_path() / "tdp_engine_backends.json";
    {
        std::ofstream output(registry_config);
        output
            << R"({"version":1,"auto_order":{"document":["pdf"],"ocr":["noop"],"layout":["text"],"table":["text"]}})";
    }
    backend_options.registry_config = registry_config;
    doc_parser::pipeline::EngineConfig engine_config = doc_parser::pipeline::defaultEngineConfig();
    engine_config.backends = backend_options;
    doc_parser::pipeline::DocumentEngine engine(engine_config, registry);
    ASSERT_TRUE(engine.isReady()) << engine.initializationStatus().message();
    std::filesystem::remove(registry_config);

    const std::filesystem::path output_root = std::filesystem::temp_directory_path() / "tdp_engine_test";
    std::filesystem::remove_all(output_root);

    doc_parser::pipeline::DocumentParseOptions first;
    first.input_path = std::filesystem::path(DOC_PARSER_TEST_FIXTURE_DIR) / "pdfs" / "pdfjs-basicapi.pdf";
    first.output_directory = output_root / "first";
    first.render.dpi = 72;
    doc_parser::pipeline::ParseResult first_result = engine.parse(first);
    ASSERT_TRUE(first_result.ok()) << first_result.status.message();
    EXPECT_FALSE(std::filesystem::exists(first.output_directory / "document.json"));

    const auto document_exporter = doc_parser::exporter::createDefaultDocumentExporter();
    ASSERT_NE(document_exporter, nullptr);
    ASSERT_TRUE(
        document_exporter
            ->write({false, first.output_directory / "document.json", &first_result.document, &first_result.artifacts})
            .okStatus());
    EXPECT_TRUE(std::filesystem::is_regular_file(first.output_directory / "document.json"));

    doc_parser::pipeline::DocumentParseOptions second = first;
    second.output_directory = output_root / "second";
    const doc_parser::pipeline::ParseResult second_result = engine.parse(second);
    ASSERT_TRUE(second_result.ok()) << second_result.status.message();
    EXPECT_FALSE(std::filesystem::exists(second.output_directory / "document.json"));

    doc_parser::pipeline::DocumentParseOptions missing = first;
    missing.input_path = output_root / "missing.pdf";
    missing.output_directory = output_root / "missing";
    const doc_parser::pipeline::ParseResult missing_result = engine.parse(missing);
    EXPECT_FALSE(missing_result.ok());
    EXPECT_EQ(missing_result.status.stage(), "open");
    EXPECT_EQ(missing_result.status.code(), "open_document_failed");

    EXPECT_EQ(counts->document, 1);
    EXPECT_EQ(counts->ocr, 1);
    EXPECT_EQ(counts->layout, 1);
    EXPECT_EQ(counts->table, 1);

    std::filesystem::remove_all(output_root);
}

TEST(DocumentPipelineTest, RunUsesTheExplicitBackendRegistry) {
    auto counts = std::make_shared<FactoryCounts>();
    doc_parser::pipeline::BackendRegistry registry;
    ASSERT_TRUE(registry.registerDocument("worker-pdf", [counts] {
        ++counts->document;
        return doc_parser::document_source::createDocumentSource("pdf");
    }));
    ASSERT_TRUE(registry.registerOcr("worker-noop", [counts] {
        ++counts->ocr;
        return std::make_unique<doc_parser::ocr::NoopOcrBackend>();
    }));
    ASSERT_TRUE(registry.registerLayout("worker-text-layout", [counts] {
        ++counts->layout;
        return std::make_unique<doc_parser::layout::TextLayoutModelBackend>();
    }));
    ASSERT_TRUE(registry.registerTable("worker-text-table", [counts] {
        ++counts->table;
        return std::make_unique<doc_parser::table::TextTableStructureBackend>();
    }));

    const std::filesystem::path output_root = std::filesystem::temp_directory_path() / "tdp_explicit_registry_pipeline_"
                                                                                       "test";
    const std::filesystem::path registry_config = output_root / "backends.json";
    std::filesystem::remove_all(output_root);
    std::filesystem::create_directories(output_root);
    {
        std::ofstream output(registry_config);
        output
            << R"({"version":1,"auto_order":{"document":["worker-pdf"],"ocr":["worker-noop"],"layout":["worker-text-layout"],"table":["worker-text-table"]}})";
    }

    doc_parser::pipeline::PipelineRunOptions options;
    options.input_path = std::filesystem::path(DOC_PARSER_TEST_FIXTURE_DIR) / "pdfs" / "pdfjs-basicapi.pdf";
    options.output_directory = output_root / "output";
    options.render.dpi = 72;
    options.backends.document = "worker-pdf";
    options.backends.ocr = "worker-noop";
    options.backends.layout = "worker-text-layout";
    options.backends.table = "worker-text-table";
    options.backends.registry_config = registry_config;

    doc_parser::pipeline::NullStageObserver observer;
    const doc_parser::pipeline::DocumentPipeline pipeline;
    const doc_parser::common::Status status = pipeline.run(options, registry, observer);

    ASSERT_TRUE(status.okStatus()) << status.stage() << ": " << status.message();
    EXPECT_TRUE(std::filesystem::is_regular_file(options.output_directory / "document.json"));
    EXPECT_EQ(counts->document, 1);
    EXPECT_EQ(counts->ocr, 1);
    EXPECT_EQ(counts->layout, 1);
    EXPECT_EQ(counts->table, 1);
    std::filesystem::remove_all(output_root);
}

TEST(DocumentEngineTest, InitializationExposesStructuredBackendFailure) {
    doc_parser::pipeline::EngineConfig config = doc_parser::pipeline::defaultEngineConfig();
    config.backends.document = "pdf";
    config.backends.ocr = "not-registered";
    config.backends.layout = "text";
    config.backends.table = "text";

    doc_parser::pipeline::DocumentEngine engine(std::move(config));

    ASSERT_FALSE(engine.isReady());
    const doc_parser::common::Status& status = engine.initializationStatus();
    EXPECT_EQ(status.stage(), "configure");
    EXPECT_EQ(status.code(), "configure.backend_unknown");
    EXPECT_NE(status.message().find("not-registered"), std::string::npos);
}

#if DOC_PARSER_ENABLE_ONNXRUNTIME
TEST(DocumentEngineTest, DefaultConfigKeepsConfiguredModelPaths) {
    const doc_parser::pipeline::EngineConfig config = doc_parser::pipeline::defaultEngineConfig();

    EXPECT_TRUE(config.paddle_ocr.detection_model.is_absolute());
    EXPECT_TRUE(config.paddle_ocr.recognition_model.is_absolute());
    EXPECT_TRUE(config.paddle_ocr.character_dict.is_absolute());
    EXPECT_TRUE(config.doclaynet.model_path.is_absolute());
    EXPECT_TRUE(config.paddle_layout.model_path.is_absolute());
    EXPECT_TRUE(config.table_transformer.detection_model_path.is_absolute());
    EXPECT_TRUE(config.table_transformer.structure_model_path.is_absolute());
}

TEST(DocumentEngineTest, InitializationPreservesModelPathFailureReason) {
    doc_parser::pipeline::EngineConfig config = doc_parser::pipeline::defaultEngineConfig();
    config.backends.document = "pdf";
    config.backends.ocr = "paddle";
    config.backends.layout = "text";
    config.backends.table = "text";
    config.paddle_ocr.detection_model = "/missing/test-det.onnx";

    doc_parser::pipeline::DocumentEngine engine(std::move(config));

    ASSERT_FALSE(engine.isReady());
    const doc_parser::common::Status& status = engine.initializationStatus();
    EXPECT_EQ(status.stage(), "configure");
    EXPECT_EQ(status.code(), "configure.backend_unavailable");
    EXPECT_NE(status.message().find("/missing/test-det.onnx"), std::string::npos);
}
#endif

TEST(DocumentEngineTest, LegacyEnvironmentIsAnExplicitConfigAdapter) {
    ASSERT_EQ(setenv("DOCUMENT_INTELLIGENCE_ENGINE_DOCLAYNET_CONFIDENCE", "0.73", 1), 0);
    ASSERT_EQ(setenv("DOCUMENT_INTELLIGENCE_ENGINE_TABLE_CROP_PADDING", "31", 1), 0);
    ASSERT_EQ(setenv("DOCUMENT_INTELLIGENCE_ENGINE_TESSERACT_LANG", "eng+chi_sim", 1), 0);

    const doc_parser::pipeline::EngineConfig config = doc_parser::pipeline::engineConfigFromEnvironment();

    EXPECT_DOUBLE_EQ(config.doclaynet.confidence_threshold, 0.73);
    EXPECT_EQ(config.table_transformer.crop_padding, 31);
    EXPECT_EQ(config.tesseract.language, "eng+chi_sim");

    unsetenv("DOCUMENT_INTELLIGENCE_ENGINE_DOCLAYNET_CONFIDENCE");
    unsetenv("DOCUMENT_INTELLIGENCE_ENGINE_TABLE_CROP_PADDING");
    unsetenv("DOCUMENT_INTELLIGENCE_ENGINE_TESSERACT_LANG");
}
