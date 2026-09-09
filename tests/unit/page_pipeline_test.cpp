#include "document_intelligence_engine/document_engine.h"
#include "export/json_document_exporter.h"
#include "pipeline/backend_registry.h"
#include "pipeline/document_engine_internal.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace document = doc_parser::document;
namespace pipeline = doc_parser::pipeline;
namespace source = doc_parser::document_source;

struct Probe {
    int pages = 3;
    bool page_rendering = true;
    bool page_text = true;
    int render_failure = 0;
    int text_failure = 0;
    bool wrong_identity = false;
    bool wrong_count = false;
    bool slow_ocr = false;
    std::vector<std::string> calls;
};

class Source final : public source::IDocumentSource, public source::IPageRenderer, public source::INativeTextExtractor {
public:
    explicit Source(std::shared_ptr<Probe> probe) : probe_(std::move(probe)) {}
    bool open(const std::filesystem::path& path) override {
        path_ = path.string();
        return true;
    }
    std::string sourcePath() const override { return path_; }
    std::string sourceType() const override { return "pdf"; }
    int pageCount() const override { return probe_->pages; }
    bool supportsPageRendering() const override { return probe_->page_rendering; }
    bool supportsPageTextExtraction() const override { return probe_->page_text; }
    bool renderPage(const source::RenderRequest& request, int index, document::PageArtifact& page) const override {
        probe_->calls.push_back("render" + std::to_string(index + 1));
        if (probe_->render_failure == index + 1)
            return false;
        page = makePage(request, index);
        return true;
    }
    bool renderPages(const source::RenderRequest& request, std::vector<document::PageArtifact>& pages) const override {
        probe_->calls.push_back("render_all");
        for (int index = 0; index < probe_->pages - (probe_->wrong_count ? 1 : 0); ++index) {
            pages.push_back(makePage(request, index));
        }
        return true;
    }
    bool extractPageNativeText(const source::NativeTextRequest&, int index, document::PageText& text) const override {
        probe_->calls.push_back("native" + std::to_string(index + 1));
        text = emptyText(index);
        return probe_->text_failure != index + 1;
    }
    bool extractNativeText(const source::NativeTextRequest&, std::vector<document::PageText>& texts) const override {
        probe_->calls.push_back("native_all");
        for (int index = 0; index < probe_->pages; ++index)
            texts.push_back(emptyText(index));
        return true;
    }

private:
    document::PageText emptyText(int index) const {
        document::PageText text;
        text.page_index = index;
        text.page_number = index + 1;
        return text;
    }
    document::PageArtifact makePage(const source::RenderRequest& request, int index) const {
        document::PageArtifact page;
        page.page_index = probe_->wrong_identity ? index + 1 : index;
        page.page_number = index + 1;
        page.relative_image = "pages/page_" + std::to_string(index + 1) + ".png";
        page.output_path = request.output_root / page.relative_image;
        page.width = 200;
        page.height = 200;
        std::filesystem::create_directories(page.output_path.parent_path());
        std::ofstream(page.output_path) << "fake renderer output";
        return page;
    }
    std::shared_ptr<Probe> probe_;
    std::string path_;
};

class Ocr final : public doc_parser::ocr::IOcrBackend {
public:
    explicit Ocr(std::shared_ptr<Probe> probe) : probe_(std::move(probe)) {}
    bool recognize(const doc_parser::ocr::OcrRequest& request, doc_parser::ocr::OcrResult& result) const override {
        probe_->calls.push_back("ocr" + std::to_string(request.page.page_number));
        if (probe_->slow_ocr)
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        result.page_text.page_index = request.page.page_index;
        result.page_text.page_number = request.page.page_number;
        result.page_text.has_text = true;
        result.page_text.preferred_source = document::TextSource::Ocr;
        document::TextLine line;
        line.text = "page text " + std::to_string(request.page.page_number);
        line.bbox = {10, 50, 180, 70};
        line.source = document::TextSource::Ocr;
        result.page_text.lines.push_back(std::move(line));
        return true;
    }

private:
    std::shared_ptr<Probe> probe_;
};

class Layout final : public doc_parser::layout::ILayoutBackend {
public:
    explicit Layout(std::shared_ptr<Probe> probe) : probe_(std::move(probe)) {}
    bool analyze(const doc_parser::layout::LayoutRequest& request,
                 doc_parser::layout::LayoutResult& result) const override {
        probe_->calls.push_back("layout" + std::to_string(request.page.page_number));
        const bool ok = doc_parser::layout::TextLayoutModelBackend().analyze(request, result);
        result.diagnostics.push_back({"layout.fixture", "layout warning", "layout", request.page.page_number, {}});
        return ok;
    }

private:
    std::shared_ptr<Probe> probe_;
};

class Table final : public doc_parser::table::ITableBackend {
public:
    explicit Table(std::shared_ptr<Probe> probe) : probe_(std::move(probe)) {}
    bool recognize(const doc_parser::table::TableRequest& request,
                   doc_parser::table::TableResult& result) const override {
        probe_->calls.push_back("table" + std::to_string(request.page.page_number));
        result.tables.page_index = request.page.page_index;
        result.tables.page_number = request.page.page_number;
        document::Table table;
        table.id = "page_" + std::to_string(request.page.page_number) + "_table_1";
        table.page_index = request.page.page_index;
        table.page_number = request.page.page_number;
        table.bbox = {10, request.page.page_number == 1 ? 170.0 : 2.0, 190, 198};
        table.columns.push_back({0, {10, table.bbox.y0, 100, 198}, 1.0});
        table.columns.push_back({1, {100, table.bbox.y0, 190, 198}, 1.0});
        result.tables.tables.push_back(std::move(table));
        result.diagnostics.push_back({"table.fixture", "table warning", "table", request.page.page_number, {}});
        return true;
    }

private:
    std::shared_ptr<Probe> probe_;
};

class Observer final : public pipeline::IStageObserver {
public:
    explicit Observer(std::shared_ptr<Probe> probe) : probe_(std::move(probe)) {}
    void onStageStarted(const pipeline::StageStartedInfo& info) override { ++starts[info.stage]; }
    void onStageProgress(const pipeline::StageProgressInfo& info) override {
        EXPECT_EQ(starts[info.stage], 1);
        EXPECT_EQ(completions[info.stage], 0);
        EXPECT_GE(info.completed, progress[info.stage]);
        EXPECT_LE(info.completed, info.total);
        progress[info.stage] = info.completed;
    }
    void onArtifactReady(const pipeline::StageArtifactInfo& info) override {
        if (info.kind == "page_image") {
            EXPECT_TRUE(std::filesystem::exists(info.path));
            if (first_artifact_calls.empty())
                first_artifact_calls = probe_->calls;
        }
    }
    void onStageCompleted(const pipeline::StageCompletedInfo& info) override {
        EXPECT_EQ(starts[info.stage], 1);
        EXPECT_GE(info.duration_ms, 0);
        ++completions[info.stage];
    }
    void onStageFailed(const pipeline::StageFailedInfo& info) override { failures.push_back(info); }
    void onStageWarning(const doc_parser::common::Diagnostic& warning) override { warnings.push_back(warning.stage); }
    std::map<std::string, int> starts, progress, completions;
    std::vector<std::string> first_artifact_calls, warnings;
    std::vector<pipeline::StageFailedInfo> failures;

private:
    std::shared_ptr<Probe> probe_;
};

class PagePipelineTest : public testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               ("tdp_page_pipeline_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(root));
        options.input_path = root / "fixture.pdf";
        std::ofstream(options.input_path) << "fake source for pipeline scheduling";
        options.output_directory = root / "output";
    }
    void TearDown() override { std::filesystem::remove_all(root); }
    pipeline::DocumentEngine engine() {
        auto config = pipeline::defaultEngineConfig();
        auto registry = pipeline::createDefaultBackendRegistry(config);
        registry.registerDocument("fixture", [probe = probe] {
            auto backend = std::make_unique<Source>(probe);
            source::DocumentSourceBundle bundle;
            bundle.renderer = backend.get();
            bundle.native_text_extractor = backend.get();
            bundle.source = std::move(backend);
            return bundle;
        });
        registry.registerOcr("fixture", [probe = probe] { return std::make_unique<Ocr>(probe); });
        registry.registerLayout("fixture", [probe = probe] { return std::make_unique<Layout>(probe); });
        registry.registerTable("fixture", [probe = probe] { return std::make_unique<Table>(probe); });
        config.backends.document = config.backends.ocr = config.backends.layout = config.backends.table = "fixture";
        return pipeline::DocumentEngineInternalAccess::create(config, registry);
    }
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    pipeline::DocumentParseOptions options;
    std::filesystem::path root;
};

TEST_F(PagePipelineTest, ProcessesEachPageBeforeRenderingNextAndLinksThreePageTables) {
    auto parser = engine();
    Observer observer(probe);
    const auto result = parser.parse(options, observer);
    ASSERT_TRUE(result.ok()) << result.status.message();
    EXPECT_EQ(observer.first_artifact_calls, std::vector<std::string>({"render1"}));
    EXPECT_EQ(probe->calls,
              std::vector<std::string>({"render1",
                                        "native1",
                                        "ocr1",
                                        "layout1",
                                        "table1",
                                        "render2",
                                        "native2",
                                        "ocr2",
                                        "layout2",
                                        "table2",
                                        "render3",
                                        "native3",
                                        "ocr3",
                                        "layout3",
                                        "table3"}));
    for (const auto& stage : {"render", "text", "layout", "table"}) {
        EXPECT_EQ(observer.starts[stage], 1);
        EXPECT_EQ(observer.completions[stage], 1);
        EXPECT_EQ(observer.progress[stage], 3);
    }
    ASSERT_EQ(result.artifacts.pages.size(), 3U);
    const auto& first = result.artifacts.pages[0].tables.tables[0];
    const auto& middle = result.artifacts.pages[1].tables.tables[0];
    const auto& last = result.artifacts.pages[2].tables.tables[0];
    EXPECT_TRUE(first.continues_on_next_page);
    EXPECT_TRUE(middle.continues_from_previous_page);
    EXPECT_TRUE(middle.continues_on_next_page);
    EXPECT_TRUE(last.continues_from_previous_page);
    EXPECT_EQ(first.continuation_group_id, last.continuation_group_id);
    EXPECT_FALSE(first.continuation_group_id.empty());
    EXPECT_EQ(observer.warnings, std::vector<std::string>({"layout", "table", "layout", "table", "layout", "table"}));
    ASSERT_EQ(result.document.warnings.size(), 2U);
    EXPECT_EQ(result.document.warnings[0].stage, "layout");
    EXPECT_EQ(result.document.warnings[1].stage, "table");
    EXPECT_EQ(result.document.warnings[0].occurrence_count, 3U);
}

TEST_F(PagePipelineTest, LegacyCapabilitiesLoadWholeDocumentsOnlyOnceWithIdenticalOutput) {
    auto parser = engine();
    const auto incremental = parser.parse(options);
    ASSERT_TRUE(incremental.ok());
    probe->calls.clear();
    probe->page_rendering = probe->page_text = false;
    const auto legacy = parser.parse(options);
    ASSERT_TRUE(legacy.ok()) << legacy.status.message();
    EXPECT_EQ(std::count(probe->calls.begin(), probe->calls.end(), "render_all"), 1);
    EXPECT_EQ(std::count(probe->calls.begin(), probe->calls.end(), "native_all"), 1);
    EXPECT_EQ(std::count(probe->calls.begin(), probe->calls.end(), "render1"), 0);
    const doc_parser::exporter::JsonDocumentExporter exporter;
    const auto first = exporter.serialize({true, &incremental.document, &incremental.artifacts});
    const auto second = exporter.serialize({true, &legacy.document, &legacy.artifacts});
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first.json, second.json);
}

TEST_F(PagePipelineTest, FailureStopsBeforeRenderingLaterPagesAndEngineCanBeReused) {
    probe->text_failure = 2;
    auto parser = engine();
    Observer observer(probe);
    const auto failed = parser.parse(options, observer);
    EXPECT_FALSE(failed.ok());
    EXPECT_EQ(failed.status.code(), "text.native_extraction_failed");
    EXPECT_EQ(failed.status.stage(), "text");
    EXPECT_FALSE(std::filesystem::exists(options.output_directory / "pages/page_3.png"));
    EXPECT_EQ(observer.progress["table"], 1);
    ASSERT_EQ(observer.failures.size(), 1U);
    probe->text_failure = 0;
    EXPECT_TRUE(parser.parse(options).ok());

    options.output_directory = root / "render-failure";
    probe->render_failure = 2;
    const auto render_failed = parser.parse(options);
    EXPECT_EQ(render_failed.status.code(), "render_failed");
    EXPECT_TRUE(render_failed.status.retryable());
    EXPECT_FALSE(std::filesystem::exists(options.output_directory / "pages/page_3.png"));
}

TEST_F(PagePipelineTest, DeadlineStopsAtTheNextSubstageWithoutRenderingTheNextPage) {
    probe->slow_ocr = true;
    options.timeout_seconds = 1;
    auto parser = engine();
    Observer observer(probe);
    const auto result = parser.parse(options, observer);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status.code(), "run_timeout");
    EXPECT_EQ(result.status.stage(), "layout");
    EXPECT_EQ(probe->calls, std::vector<std::string>({"render1", "native1", "ocr1"}));
    EXPECT_FALSE(std::filesystem::exists(options.output_directory / "pages/page_2.png"));
    ASSERT_EQ(observer.failures.size(), 1U);
}

TEST_F(PagePipelineTest, RejectsRendererIdentityAndLegacyPageCountMismatches) {
    auto parser = engine();
    probe->wrong_identity = true;
    EXPECT_EQ(parser.parse(options).status.code(), "render.page_identity_mismatch");
    probe->wrong_identity = false;
    probe->page_rendering = false;
    probe->wrong_count = true;
    EXPECT_EQ(parser.parse(options).status.code(), "render.page_count_mismatch");
}

TEST_F(PagePipelineTest, EmptyDocumentCompletesEachStageOnce) {
    probe->pages = 0;
    auto parser = engine();
    Observer observer(probe);
    const auto result = parser.parse(options, observer);
    ASSERT_TRUE(result.ok()) << result.status.message();
    for (const auto& stage : {"render", "text", "layout", "table"}) {
        EXPECT_EQ(observer.starts[stage], 1);
        EXPECT_EQ(observer.completions[stage], 1);
        EXPECT_EQ(observer.progress[stage], 0);
    }
}

} // namespace
