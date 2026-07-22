#include "table/table_backend.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace {

bool parseArgs(int argc, char** argv, std::filesystem::path& ground_truth, std::filesystem::path& output) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if ((argument == "--ground-truth" || argument == "--output") && index + 1 < argc) {
            const std::filesystem::path value = argv[++index];
            if (argument == "--ground-truth") {
                ground_truth = value;
            } else {
                output = value;
            }
        } else {
            return false;
        }
    }
    return !ground_truth.empty() && !output.empty();
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path ground_truth_path;
    std::filesystem::path output_path;
    if (!parseArgs(argc, argv, ground_truth_path, output_path)) {
        std::cerr << "Usage: " << argv[0] << " --ground-truth ground_truth.json --output predictions.json\n";
        return 2;
    }

    const doc_parser::table::TableTransformerOnnxBackend backend;
    if (!backend.isAvailable()) {
        std::cerr << "Table Transformer ONNX models are unavailable\n";
        return 77;
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
    predictions["task"] = "table_structure";
    predictions["dataset"] = ground_truth.value("dataset", "PubTables-1M");
    predictions["metadata"] = {
        {"backend", "table_transformer_onnx"},
        {"detection_model", backend.config().detection_model_path.string()},
        {"structure_model", backend.config().structure_model_path.string()},
        {"detection_confidence_threshold", backend.config().detection_confidence_threshold},
        {"structure_confidence_threshold", backend.config().structure_confidence_threshold},
        {"crop_padding", backend.config().crop_padding},
    };
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
        doc_parser::document::PageLayout layout;
        layout.page_index = page.page_index;
        layout.page_number = page.page_number;
        doc_parser::table::TableResult result;
        if (!backend.recognize({page, text, layout}, result)) {
            std::cerr << "Table inference failed for " << page.output_path << '\n';
            return 1;
        }

        nlohmann::json objects = nlohmann::json::array();
        for (const doc_parser::document::Table& table : result.tables.tables) {
            for (const doc_parser::document::TableStructureObject& object : table.structure_objects) {
                objects.push_back({
                    {"label", object.label},
                    {"score", object.confidence},
                    {"bbox", {object.bbox.x0, object.bbox.y0, object.bbox.x1, object.bbox.y1}},
                });
            }
        }
        predictions["samples"].push_back({
            {"id", sample.at("id")},
            {"image", sample.value("image", "")},
            {"objects", objects},
        });
        std::cout << sample.at("id") << " tables=" << result.tables.tables.size() << " objects=" << objects.size()
                  << '\n';
    }

    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream output(output_path);
    output << predictions.dump(2) << '\n';
    return output ? 0 : 1;
}
