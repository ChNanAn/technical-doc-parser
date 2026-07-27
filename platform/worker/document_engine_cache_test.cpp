#include "document_engine_cache.h"
#include "document_source/document_source_factory.h"
#include "layout/layout_backend.h"
#include "ocr/ocr_backend.h"
#include "pipeline/backend_registry.h"
#include "pipeline/stage_observer.h"
#include "table/table_backend.h"
#include "worker_document_processor.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct FactoryCounts {
    int document = 0;
    int layout = 0;
    int table = 0;
    std::map<std::string, int> ocr;
};

class StubDocumentSource final : public doc_parser::document_source::IDocumentSource {
public:
    bool open(const std::filesystem::path& input_path) override {
        input_path_ = input_path;
        return true;
    }

    std::string sourcePath() const override { return input_path_.string(); }
    std::string sourceType() const override { return "stub"; }
    int pageCount() const override { return 0; }

private:
    std::filesystem::path input_path_;
};

doc_parser::document_source::DocumentSourceBundle stubDocumentSource() {
    doc_parser::document_source::DocumentSourceBundle bundle;
    bundle.source = std::make_unique<StubDocumentSource>();
    return bundle;
}

doc_parser::pipeline::BackendRegistry cacheTestRegistry(const std::shared_ptr<FactoryCounts>& counts) {
    doc_parser::pipeline::BackendRegistry registry;
    EXPECT_TRUE(registry.registerDocument("stub", [counts] {
        ++counts->document;
        return stubDocumentSource();
    }));
    for (const std::string name : {"ocr-a", "ocr-b", "ocr-c"}) {
        EXPECT_TRUE(registry.registerOcr(name, [counts, name] {
            ++counts->ocr[name];
            return std::make_unique<doc_parser::ocr::NoopOcrBackend>();
        }));
    }
    EXPECT_TRUE(registry.registerLayout("layout", [counts] {
        ++counts->layout;
        return std::make_unique<doc_parser::layout::TextLayoutModelBackend>();
    }));
    EXPECT_TRUE(registry.registerTable("table", [counts] {
        ++counts->table;
        return std::make_unique<doc_parser::table::TextTableStructureBackend>();
    }));
    return registry;
}

std::filesystem::path writeCacheRegistryConfig(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "nested");
    const std::filesystem::path config = root / "backends.json";
    std::ofstream output(config);
    output
        << R"({"version":1,"auto_order":{"document":["stub"],"ocr":["ocr-a","ocr-b","ocr-c"],"layout":["layout"],"table":["table"]}})";
    return config;
}

doc_parser::pipeline::BackendOptions cacheBackends(const std::filesystem::path& config, std::string ocr) {
    doc_parser::pipeline::BackendOptions backends;
    backends.document = "stub";
    backends.ocr = std::move(ocr);
    backends.layout = "layout";
    backends.table = "table";
    backends.registry_config = config;
    return backends;
}

class RecordingObserver final : public doc_parser::pipeline::IStageObserver {
public:
    void onStageStarted(const doc_parser::pipeline::StageStartedInfo& info) override { started.push_back(info.stage); }
    void onStageProgress(const doc_parser::pipeline::StageProgressInfo&) override {}
    void onArtifactReady(const doc_parser::pipeline::StageArtifactInfo& info) override {
        artifacts.push_back(info.kind);
    }
    void onStageCompleted(const doc_parser::pipeline::StageCompletedInfo& info) override {
        completed.push_back(info.stage);
    }
    void onStageFailed(const doc_parser::pipeline::StageFailedInfo& info) override { failed.push_back(info.stage); }

    std::vector<std::string> started;
    std::vector<std::string> completed;
    std::vector<std::string> artifacts;
    std::vector<std::string> failed;
};

std::filesystem::path testRoot(const std::string& name) {
    return std::filesystem::temp_directory_path() / (name + "_" + std::to_string(getpid()));
}

} // namespace

TEST(DocumentEngineCacheTest, ReusesNormalizedBackendKeysAndEvictsLeastRecentlyUsedEngine) {
    const std::filesystem::path root = testRoot("tdp_worker_engine_cache_test");
    std::filesystem::remove_all(root);
    const std::filesystem::path config = writeCacheRegistryConfig(root);
    const auto counts = std::make_shared<FactoryCounts>();
    const doc_parser::pipeline::BackendRegistry registry = cacheTestRegistry(counts);
    doc_parser::platform::DocumentEngineCache cache(doc_parser::pipeline::defaultEngineConfig(), registry, 2);

    doc_parser::pipeline::BackendOptions first_options = cacheBackends(config, "ocr-a");
    const doc_parser::platform::DocumentEngineLookup first = cache.get(first_options);
    ASSERT_TRUE(first.ok()) << first.status.message();
    EXPECT_FALSE(first.cache_hit);

    first_options.registry_config = root / "nested" / ".." / "backends.json";
    const doc_parser::platform::DocumentEngineLookup repeated = cache.get(first_options);
    EXPECT_TRUE(repeated.cache_hit);
    EXPECT_EQ(repeated.engine, first.engine);

    const doc_parser::platform::DocumentEngineLookup second = cache.get(cacheBackends(config, "ocr-b"));
    EXPECT_FALSE(second.cache_hit);
    EXPECT_TRUE(cache.get(cacheBackends(config, "ocr-a")).cache_hit);
    const doc_parser::platform::DocumentEngineLookup third = cache.get(cacheBackends(config, "ocr-c"));
    EXPECT_FALSE(third.cache_hit);
    EXPECT_EQ(cache.size(), 2U);
    EXPECT_FALSE(cache.get(cacheBackends(config, "ocr-b")).cache_hit);
    EXPECT_EQ(counts->ocr["ocr-a"], 1);
    EXPECT_EQ(counts->ocr["ocr-b"], 2);
    EXPECT_EQ(counts->ocr["ocr-c"], 1);

    std::filesystem::remove_all(root);
}

TEST(DocumentEngineCacheTest, FailedCandidateDoesNotEvictReadyEngines) {
    const std::filesystem::path root = testRoot("tdp_worker_engine_cache_failure_test");
    std::filesystem::remove_all(root);
    const std::filesystem::path config = writeCacheRegistryConfig(root);
    const auto counts = std::make_shared<FactoryCounts>();
    const doc_parser::pipeline::BackendRegistry registry = cacheTestRegistry(counts);
    doc_parser::platform::DocumentEngineCache cache(doc_parser::pipeline::defaultEngineConfig(), registry, 2);

    ASSERT_TRUE(cache.get(cacheBackends(config, "ocr-a")).ok());
    ASSERT_TRUE(cache.get(cacheBackends(config, "ocr-b")).ok());

    const doc_parser::platform::DocumentEngineLookup failed = cache.get(cacheBackends(config, "missing"));
    EXPECT_FALSE(failed.ok());
    EXPECT_EQ(failed.engine, nullptr);
    EXPECT_EQ(failed.status.code(), "configure.backend_unknown");
    EXPECT_NE(failed.status.message().find("missing"), std::string::npos);
    EXPECT_EQ(cache.size(), 2U);
    EXPECT_TRUE(cache.get(cacheBackends(config, "ocr-a")).cache_hit);
    EXPECT_TRUE(cache.get(cacheBackends(config, "ocr-b")).cache_hit);
    EXPECT_EQ(counts->ocr["ocr-a"], 1);
    EXPECT_EQ(counts->ocr["ocr-b"], 1);

    std::filesystem::remove_all(root);
}

TEST(DocumentEngineCacheTest, RejectsZeroCapacity) {
    const auto counts = std::make_shared<FactoryCounts>();
    const doc_parser::pipeline::BackendRegistry registry = cacheTestRegistry(counts);
    EXPECT_THROW(doc_parser::platform::DocumentEngineCache(doc_parser::pipeline::defaultEngineConfig(), registry, 0),
                 std::invalid_argument);
}

TEST(DocumentEngineCacheTest, RunSelectionInheritsOnlyTheDefaultRegistryConfig) {
    doc_parser::pipeline::BackendOptions defaults;
    defaults.document = "default-document";
    defaults.ocr = "default-ocr";
    defaults.layout = "default-layout";
    defaults.table = "default-table";
    defaults.registry_config = "/worker/backends.json";

    doc_parser::pipeline::BackendOptions requested;
    requested.document = "pdf";
    requested.ocr = "noop";
    requested.layout = "text";
    requested.table = "text";
    const doc_parser::pipeline::BackendOptions inherited =
        doc_parser::platform::effectiveBackendOptions(defaults, requested);
    EXPECT_EQ(inherited.document, "pdf");
    EXPECT_EQ(inherited.ocr, "noop");
    EXPECT_EQ(inherited.layout, "text");
    EXPECT_EQ(inherited.table, "text");
    EXPECT_EQ(inherited.registry_config, "/worker/backends.json");

    requested.registry_config = "/run/backends.json";
    const doc_parser::pipeline::BackendOptions overridden =
        doc_parser::platform::effectiveBackendOptions(defaults, requested);
    EXPECT_EQ(overridden.registry_config, "/run/backends.json");
}

#ifdef DOC_PARSER_WORKER_TEST_PDFIUM
TEST(WorkerDocumentProcessorTest, ReusesEngineAndPreservesExportEvents) {
    const std::filesystem::path root = testRoot("tdp_worker_processor_test");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path config = root / "backends.json";
    {
        std::ofstream output(config);
        output
            << R"({"version":1,"auto_order":{"document":["pdf"],"ocr":["noop"],"layout":["text"],"table":["text"]}})";
    }

    const auto counts = std::make_shared<FactoryCounts>();
    doc_parser::pipeline::BackendRegistry registry;
    ASSERT_TRUE(registry.registerDocument("pdf", [counts] {
        ++counts->document;
        return doc_parser::document_source::createDocumentSource("pdf");
    }));
    ASSERT_TRUE(registry.registerOcr("noop", [counts] {
        ++counts->ocr["noop"];
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

    doc_parser::pipeline::BackendOptions backends;
    backends.document = "pdf";
    backends.ocr = "noop";
    backends.layout = "text";
    backends.table = "text";
    backends.registry_config = config;
    doc_parser::platform::WorkerDocumentProcessor processor(doc_parser::pipeline::defaultEngineConfig(), registry, 2);
    RecordingObserver observer;

    doc_parser::pipeline::DocumentParseOptions options;
    options.input_path = std::filesystem::path(DOC_PARSER_TEST_FIXTURE_DIR) / "pdfs" / "pdfjs-basicapi.pdf";
    options.output_directory = root / "first";
    options.run_id = "run_worker_processor_test";
    options.render.dpi = 72;
    options.timeout_seconds = 30;
    ASSERT_TRUE(processor.process(options, backends, observer).okStatus());
    EXPECT_TRUE(std::filesystem::is_regular_file(options.output_directory / "document.json"));
    EXPECT_TRUE(std::filesystem::is_regular_file(options.output_directory / "document.md"));
    EXPECT_TRUE(std::filesystem::is_regular_file(options.output_directory / "document.html"));
    {
        std::ifstream input(options.output_directory / "document.json");
        const nlohmann::json document = nlohmann::json::parse(input);
        EXPECT_EQ(document["producer"]["run_id"], options.run_id);
    }

    options.output_directory = root / "second";
    ASSERT_TRUE(processor.process(options, backends, observer).okStatus());
    EXPECT_EQ(processor.cachedEngineCount(), 1U);
    EXPECT_EQ(counts->document, 1);
    EXPECT_EQ(counts->ocr["noop"], 1);
    EXPECT_EQ(counts->layout, 1);
    EXPECT_EQ(counts->table, 1);
    EXPECT_EQ(std::count(observer.started.begin(), observer.started.end(), "export"), 2);
    EXPECT_EQ(std::count(observer.completed.begin(), observer.completed.end(), "export"), 2);
    EXPECT_EQ(std::count(observer.artifacts.begin(), observer.artifacts.end(), "document_json"), 2);
    EXPECT_EQ(std::count(observer.artifacts.begin(), observer.artifacts.end(), "document_markdown"), 2);
    EXPECT_EQ(std::count(observer.artifacts.begin(), observer.artifacts.end(), "document_html"), 2);
    EXPECT_TRUE(observer.failed.empty());

    std::filesystem::remove_all(root);
}
#endif
