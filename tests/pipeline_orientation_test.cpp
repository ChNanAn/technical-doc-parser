#include "document_intelligence_engine/document_engine.h"
#include "document_source/document_source_factory.h"
#include "export/json_document_exporter.h"
#include "pipeline/backend_registry.h"
#include "pipeline/document_engine_internal.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>

namespace {
namespace document = doc_parser::document;
namespace source = doc_parser::document_source;
namespace pipeline = doc_parser::pipeline;

void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

// Keep the model input pixels identical across rotations; PDF rendering is covered separately.
class ImageSource final : public source::IDocumentSource, public source::IPageRenderer {
public:
    bool open(const std::filesystem::path& input) override {
        input_ = input;
        image_ = cv::imread(input.string());
        return !image_.empty();
    }
    std::string sourcePath() const override { return input_.string(); }
    std::string sourceType() const override { return "png"; }
    int pageCount() const override { return 1; }
    bool renderPages(const source::RenderRequest& request, std::vector<document::PageArtifact>& pages) const override {
        pages.resize(1);
        return renderPage(request, 0, pages[0]);
    }
    bool supportsPageRendering() const override { return true; }
    bool renderPage(const source::RenderRequest& request, int index, document::PageArtifact& page) const override {
        if (index != 0)
            return false;
        std::filesystem::create_directories(request.pages_dir);
        page = {};
        page.page_number = 1;
        page.width = image_.cols;
        page.height = image_.rows;
        page.output_path = request.pages_dir / "page_1.png";
        page.relative_image = page.output_path.lexically_relative(request.output_root).generic_string();
        return cv::imwrite(page.output_path.string(), image_);
    }

private:
    std::filesystem::path input_;
    cv::Mat image_;
};

pipeline::DocumentEngine makeEngine() {
    auto config = pipeline::defaultEngineConfig();
    config.backends.document = "test-image";
    config.backends.ocr = "paddle";
    config.backends.layout = "doclaynet";
    config.backends.table = "table-transformer";
    auto registry = pipeline::createDefaultBackendRegistry(config);
    registry.registerDocument("test-image", [] {
        auto backend = std::make_unique<ImageSource>();
        source::DocumentSourceBundle bundle;
        bundle.renderer = backend.get();
        bundle.source = std::move(backend);
        return bundle;
    });
    return pipeline::DocumentEngineInternalAccess::create(config, registry);
}

std::string text(const pipeline::ParseResult& result) {
    std::string value;
    for (const auto& block : result.document.blocks)
        value += block.text + '\n';
    return value;
}

bool hasPrivateView(const std::filesystem::path& root) {
    for (const auto& entry : std::filesystem::directory_iterator(root))
        if (entry.path().filename().string().find(".die-orientation-") == 0)
            return true;
    return false;
}

class AbortAfterTables final : public pipeline::IStageObserver {
public:
    explicit AbortAfterTables(std::filesystem::path root) : root_(std::move(root)) {}
    void onStageStarted(const pipeline::StageStartedInfo& info) override {
        if (info.stage == "reading_order") {
            saw_view = hasPrivateView(root_);
            throw std::runtime_error("test observer interruption");
        }
    }
    void onStageProgress(const pipeline::StageProgressInfo&) override {}
    void onStageCompleted(const pipeline::StageCompletedInfo&) override {}
    void onStageFailed(const pipeline::StageFailedInfo&) override {}
    void onArtifactReady(const pipeline::StageArtifactInfo& info) override {
        require(info.path.string().find(".die-orientation-") == std::string::npos,
                "private image was published as an artifact");
    }
    bool saw_view = false;

private:
    std::filesystem::path root_;
};

void compareBox(const document::BBox& actual, const document::BBox& upright, const cv::Size& size) {
    require(std::abs(actual.x0 - (size.width - upright.x1)) < 1e-4 &&
                std::abs(actual.y0 - (size.height - upright.y1)) < 1e-4 &&
                std::abs(actual.x1 - (size.width - upright.x0)) < 1e-4 &&
                std::abs(actual.y1 - (size.height - upright.y0)) < 1e-4,
            "exported bbox does not match source image");
}

void check(pipeline::DocumentEngine& engine, const std::filesystem::path& input, const std::filesystem::path& root) {
    const auto pixels = cv::imread(input.string());
    require(!pixels.empty(), "missing fixture");
    cv::Mat rotated;
    cv::rotate(pixels, rotated, cv::ROTATE_180);
    const auto rotated_path = root / (input.stem().string() + "-180.png");
    require(cv::imwrite(rotated_path.string(), rotated), "cannot write fixture");
    pipeline::DocumentParseOptions options;
    options.input_path = input;
    options.output_directory = root / (input.stem().string() + "-upright");
    options.debug = true;
    const auto upright = engine.parse(options);
    require(upright.ok(), upright.status.message());
    options.input_path = rotated_path;
    options.output_directory = root / (input.stem().string() + "-rotated");
    const auto restored = engine.parse(options);
    require(restored.ok(), restored.status.message());
    std::cout << input.filename() << " upright_blocks=" << upright.document.blocks.size()
              << " rotated_blocks=" << restored.document.blocks.size()
              << " final_text_equal=" << (text(upright) == text(restored)) << '\n';
    if (text(upright) != text(restored))
        std::cout << "UPRIGHT:\n" << text(upright) << "ROTATED:\n" << text(restored) << std::flush;
    require(text(upright) == text(restored), "final pipeline text/order differs after rotation");
    require(upright.document.blocks.size() == restored.document.blocks.size(), "block count changed");
    for (std::size_t i = 0; i < upright.document.blocks.size(); ++i) {
        const auto& a = restored.document.blocks[i];
        const auto& b = upright.document.blocks[i];
        require(a.type == b.type && a.text == b.text, "block type/text changed");
        compareBox(a.bbox, b.bbox, pixels.size());
        require(a.source_refs.size() == b.source_refs.size(), "source reference count changed");
        for (std::size_t j = 0; j < a.source_refs.size(); ++j)
            compareBox(a.source_refs[j].bbox, b.source_refs[j].bbox, pixels.size());
    }
    const auto& image = restored.artifacts.pages.at(0).image;
    require(cv::norm(cv::imread(image.output_path.string()), rotated, cv::NORM_INF) == 0,
            "published page pixels changed");
    const doc_parser::exporter::JsonDocumentExporter exporter;
    const auto json = exporter.serialize({true, &restored.document, &restored.artifacts});
    require(json.ok(), "invalid Document Contract output");
    require(json.json.find(".die-orientation-") == std::string::npos, "private path leaked into exported document");
    require(!hasPrivateView(options.output_directory), "private images survived a successful parse");

    AbortAfterTables observer(options.output_directory);
    const auto interrupted = engine.parse(options, observer);
    require(!interrupted.ok() && observer.saw_view, "did not interrupt a corrected parse after the table stage");
    require(!hasPrivateView(options.output_directory), "private images survived observer interruption");
    options.image_cache_bytes = 0;
    const auto uncached = engine.parse(options);
    require(uncached.ok() && text(uncached) == text(upright), "uncached parse/engine reuse changed recovered text");
    require(!hasPrivateView(options.output_directory), "uncached parse leaked private images");
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 4)
        return 2;
    try {
        spdlog::set_level(spdlog::level::debug);
        const std::filesystem::path root = argv[1];
        std::filesystem::create_directories(root);
        auto engine = makeEngine();
        require(engine.isReady(), engine.initializationStatus().message());
        for (int i = 2; i < argc; ++i)
            check(engine, argv[i], root);
    } catch (const std::exception& error) {
        std::cerr << "pipeline orientation regression: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
