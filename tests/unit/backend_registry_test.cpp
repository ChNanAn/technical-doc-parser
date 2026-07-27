#include "pipeline/backend_registry.h"
#include "pipeline/pipeline_service_factory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

class UnavailableOcrBackend final : public doc_parser::ocr::IOcrBackend {
public:
    bool recognize(const doc_parser::ocr::OcrRequest&, doc_parser::ocr::OcrResult&) const override { return false; }
    bool isAvailable() const override { return false; }
    std::string unavailableReason() const override { return "test model file is missing"; }
};

std::filesystem::path writeConfig(const std::string& name, const std::string& contents) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path);
    output << contents;
    return path;
}

} // namespace

TEST(BackendRegistryTest, RejectsDuplicateRegistrationAndReportsUnknownBackend) {
    doc_parser::pipeline::BackendRegistry registry;
    EXPECT_TRUE(registry.registerOcr("noop", [] { return std::make_unique<doc_parser::ocr::NoopOcrBackend>(); }));
    EXPECT_FALSE(registry.registerOcr("noop", [] { return std::make_unique<doc_parser::ocr::NoopOcrBackend>(); }));
    EXPECT_FALSE(registry.registerOcr("auto", [] { return std::make_unique<doc_parser::ocr::NoopOcrBackend>(); }));
    EXPECT_EQ(registry.createOcr("missing").status, doc_parser::pipeline::BackendCreationStatus::Unknown);
    EXPECT_EQ(registry.createOcr("noop").status, doc_parser::pipeline::BackendCreationStatus::Created);
}

TEST(BackendRegistryTest, PreservesBackendAvailabilityReason) {
    doc_parser::pipeline::BackendRegistry registry;
    ASSERT_TRUE(registry.registerOcr("unavailable", [] { return std::make_unique<UnavailableOcrBackend>(); }));

    const auto creation = registry.createOcr("unavailable");

    EXPECT_EQ(creation.status, doc_parser::pipeline::BackendCreationStatus::Unavailable);
    EXPECT_EQ(creation.error_message, "test model file is missing");
}

TEST(BackendRegistryTest, ListsRegisteredBackendsForCapabilityDiscovery) {
    const doc_parser::pipeline::BackendRegistry registry = doc_parser::pipeline::createDefaultBackendRegistry();

    EXPECT_FALSE(registry.documentNames().empty());
    EXPECT_FALSE(registry.ocrNames().empty());
    EXPECT_FALSE(registry.layoutNames().empty());
    EXPECT_FALSE(registry.tableNames().empty());
}

TEST(BackendRegistryTest, LoadsValidatedAutoOrderAndDrivesServiceSelection) {
    const std::filesystem::path config_path =
        writeConfig("tdp_backend_registry_valid.json",
                    R"({"version":1,"auto_order":{"ocr":["noop"],"layout":["text"],"table":["text"]}})");
    doc_parser::pipeline::BackendOptions options;
    options.registry_config = config_path;

    const doc_parser::pipeline::PipelineServiceCreationResult result =
        doc_parser::pipeline::createPipelineServices(options);
    ASSERT_TRUE(result.status.okStatus()) << result.status.message();
    EXPECT_NE(result.trace_message.find("ocr=noop"), std::string::npos);
    EXPECT_NE(result.trace_message.find("layout=text"), std::string::npos);
    EXPECT_NE(result.trace_message.find("table=text"), std::string::npos);
    EXPECT_NE(result.trace_message.find(config_path.string()), std::string::npos);
    EXPECT_EQ(result.provenance.backends.requested.registry_config, config_path);
    EXPECT_EQ(result.provenance.backends.resolved.ocr, "noop");
    EXPECT_EQ(result.provenance.backends.resolved.layout, "text");
    EXPECT_EQ(result.provenance.backends.resolved.table, "text");
    EXPECT_EQ(result.provenance.backends.config_source, config_path.string());
    std::filesystem::remove(config_path);
}

TEST(BackendRegistryTest, RejectsUnknownAndDuplicateConfiguredBackends) {
    const doc_parser::pipeline::BackendRegistry registry = doc_parser::pipeline::createDefaultBackendRegistry();
    const std::filesystem::path unknown_path =
        writeConfig("tdp_backend_registry_unknown.json",
                    R"({"version":1,"auto_order":{"ocr":["missing"],"layout":["text"],"table":["text"]}})");
    const doc_parser::pipeline::BackendRegistryConfigResult unknown =
        doc_parser::pipeline::loadBackendRegistryConfig(unknown_path, registry);
    EXPECT_FALSE(unknown.ok);
    EXPECT_NE(unknown.error.find("unregistered backend: missing"), std::string::npos);

    const std::filesystem::path duplicate_path =
        writeConfig("tdp_backend_registry_duplicate.json",
                    R"({"version":1,"auto_order":{"ocr":["noop","noop"],"layout":["text"],"table":["text"]}})");
    const doc_parser::pipeline::BackendRegistryConfigResult duplicate =
        doc_parser::pipeline::loadBackendRegistryConfig(duplicate_path, registry);
    EXPECT_FALSE(duplicate.ok);
    EXPECT_NE(duplicate.error.find("duplicate backend: noop"), std::string::npos);

    std::filesystem::remove(unknown_path);
    std::filesystem::remove(duplicate_path);
}
