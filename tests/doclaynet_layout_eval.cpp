#include "layout/layout_backend.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace {

const char* mappedLabel(doc_parser::document::LayoutBlockType type) {
    switch (type) {
    case doc_parser::document::LayoutBlockType::Title:
        return "title";
    case doc_parser::document::LayoutBlockType::Text:
        return "text";
    case doc_parser::document::LayoutBlockType::List:
        return "list";
    case doc_parser::document::LayoutBlockType::Table:
        return "table";
    case doc_parser::document::LayoutBlockType::Figure:
        return "figure";
    case doc_parser::document::LayoutBlockType::Header:
        return "header";
    case doc_parser::document::LayoutBlockType::Footer:
        return "footer";
    case doc_parser::document::LayoutBlockType::Unknown:
        return "unknown";
    }
    return "unknown";
}

bool parseArgs(int argc,
               char** argv,
               std::string& backend,
               std::filesystem::path& ground_truth,
               std::filesystem::path& output) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if ((argument == "--backend" || argument == "--ground-truth" || argument == "--output") && index + 1 < argc) {
            const std::filesystem::path value = argv[++index];
            if (argument == "--backend") {
                backend = value.string();
            } else if (argument == "--ground-truth") {
                ground_truth = value;
            } else {
                output = value;
            }
        } else {
            return false;
        }
    }
    return (backend == "doclaynet" || backend == "paddle-layout") && !ground_truth.empty() && !output.empty();
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path ground_truth_path;
    std::filesystem::path output_path;
    std::string backend_name;
    if (!parseArgs(argc, argv, backend_name, ground_truth_path, output_path)) {
        std::cerr << "Usage: " << argv[0]
                  << " --backend doclaynet|paddle-layout --ground-truth ground_truth.json --output predictions.json\n";
        return 2;
    }

    std::unique_ptr<doc_parser::layout::ILayoutBackend> backend;
    nlohmann::json metadata;
    if (backend_name == "doclaynet") {
        auto candidate = std::make_unique<doc_parser::layout::DocLayNetOnnxBackend>();
        if (!candidate->isAvailable()) {
            std::cerr << "DocLayNet ONNX model is unavailable\n";
            return 77;
        }
        metadata = {
            {"backend", "rfdetr_doclaynet_onnx"},
            {"model", candidate->config().model_path.string()},
            {"confidence_threshold", candidate->config().confidence_threshold},
            {"input_width", candidate->config().input_width},
            {"input_height", candidate->config().input_height},
        };
        backend = std::move(candidate);
    } else {
        auto candidate = std::make_unique<doc_parser::layout::PaddleDocLayoutOnnxBackend>();
        if (!candidate->isAvailable()) {
            std::cerr << "Paddle PP-DocLayoutV3 ONNX model is unavailable\n";
            return 77;
        }
        metadata = {
            {"backend", "paddle_pp_doclayout_v3_onnx"},
            {"model", candidate->config().model_path.string()},
            {"confidence_threshold", candidate->config().confidence_threshold},
            {"input_width", candidate->config().input_width},
            {"input_height", candidate->config().input_height},
        };
        backend = std::move(candidate);
    }

    std::ifstream input(ground_truth_path);
    if (!input) {
        std::cerr << "Failed to read " << ground_truth_path << '\n';
        return 2;
    }
    const nlohmann::json ground_truth = nlohmann::json::parse(input);
    const std::filesystem::path corpus_root = ground_truth_path.parent_path();

    nlohmann::json predictions;
    predictions["version"] = 1;
    predictions["task"] = "layout";
    predictions["dataset"] = ground_truth.value("dataset", "DocLayNet");
    predictions["metadata"] = std::move(metadata);
    predictions["samples"] = nlohmann::json::array();

    for (const nlohmann::json& sample : ground_truth.value("samples", nlohmann::json::array())) {
        doc_parser::document::PageArtifact page;
        page.page_index = static_cast<int>(predictions["samples"].size());
        page.page_number = page.page_index + 1;
        page.width = sample.value("width", 0);
        page.height = sample.value("height", 0);
        page.output_path = corpus_root / sample.value("image", "");

        doc_parser::document::PageText text;
        text.page_index = page.page_index;
        text.page_number = page.page_number;
        doc_parser::layout::LayoutResult result;
        if (!backend->analyze({page, text}, result)) {
            std::cerr << "Layout inference failed for " << page.output_path << '\n';
            return 1;
        }

        nlohmann::json objects = nlohmann::json::array();
        for (const doc_parser::document::LayoutBlock& block : result.layout.blocks) {
            objects.push_back({
                {"id", block.id},
                {"label", block.source_label},
                {"mapped_label", mappedLabel(block.type)},
                {"related_block_id", block.related_block_id},
                {"score", block.confidence},
                {"bbox", {block.bbox.x0, block.bbox.y0, block.bbox.x1, block.bbox.y1}},
            });
        }
        predictions["samples"].push_back({
            {"id", sample.at("id")},
            {"image", sample.value("image", "")},
            {"objects", objects},
        });
        std::cout << sample.at("id") << " blocks=" << result.layout.blocks.size() << '\n';
    }

    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream output(output_path);
    output << predictions.dump(2) << '\n';
    return output ? 0 : 1;
}
