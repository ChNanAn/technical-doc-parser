#include "document_source/document_source_factory.h"
#include "layout/layout_backend.h"
#include "ocr/ocr_backend.h"
#include "pipeline/document_engine.h"
#include "table/table_backend.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>

namespace {

struct FactoryCounts {
    int document = 0;
    int ocr = 0;
    int layout = 0;
    int table = 0;
};

} // namespace

TEST(DocumentEngineTest, ReusesBackendInstancesAcrossDocuments) {
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
    doc_parser::pipeline::DocumentEngine engine(backend_options, registry);
    ASSERT_TRUE(engine.isReady()) << engine.initializationError();
    std::filesystem::remove(registry_config);

    const std::filesystem::path output_root = std::filesystem::temp_directory_path() / "tdp_engine_test";
    std::filesystem::remove_all(output_root);

    doc_parser::pipeline::PipelineRunOptions first;
    first.input_path = std::filesystem::path(DOC_PARSER_TEST_FIXTURE_DIR) / "pdfs" / "pdfjs-basicapi.pdf";
    first.output_directory = output_root / "first";
    first.render.dpi = 72;
    ASSERT_TRUE(engine.parse(first));

    doc_parser::pipeline::PipelineRunOptions second = first;
    second.output_directory = output_root / "second";
    ASSERT_TRUE(engine.parse(second));

    EXPECT_TRUE(std::filesystem::is_regular_file(first.output_directory / "document.json"));
    EXPECT_TRUE(std::filesystem::is_regular_file(second.output_directory / "document.json"));
    EXPECT_EQ(counts->document, 1);
    EXPECT_EQ(counts->ocr, 1);
    EXPECT_EQ(counts->layout, 1);
    EXPECT_EQ(counts->table, 1);

    std::filesystem::remove_all(output_root);
}
