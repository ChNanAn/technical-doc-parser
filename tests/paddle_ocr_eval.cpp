#include "ocr/paddle_ocr_onnx_backend.h"
#include "pipeline/engine_config.h"

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

std::string pageText(const doc_parser::document::PageText& page) {
    std::string text;
    for (const doc_parser::document::TextLine& line : page.lines) {
        if (!text.empty()) {
            text += '\n';
        }
        text += line.text;
    }
    return text;
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path ground_truth_path;
    std::filesystem::path output_path;
    if (!parseArgs(argc, argv, ground_truth_path, output_path)) {
        std::cerr << "Usage: " << argv[0] << " --ground-truth ground_truth.json --output predictions.json\n";
        return 2;
    }

    const doc_parser::pipeline::EngineConfig engine_config = doc_parser::pipeline::defaultEngineConfig();
    const doc_parser::ocr::PaddleOcrOnnxBackend backend(engine_config.paddle_ocr);
    if (!backend.isAvailable()) {
        std::cerr << "PaddleOCR ONNX models are unavailable: " << backend.unavailableReason() << '\n';
        return 1;
    }

    std::ifstream input(ground_truth_path);
    if (!input) {
        std::cerr << "Failed to read " << ground_truth_path << '\n';
        return 2;
    }
    const nlohmann::json ground_truth = nlohmann::json::parse(input);
    const std::filesystem::path corpus_root = ground_truth_path.parent_path();

    const doc_parser::ocr::PaddleOcrOnnxConfig& config = backend.config();
    nlohmann::json predictions;
    predictions["version"] = 1;
    predictions["task"] = "ocr_text";
    predictions["dataset"] = ground_truth.value("dataset", "tesseract-ocr/test");
    predictions["metadata"] = {
        {"backend", "paddleocr_onnx"},
        {"model",
         {
             {"profile", config.profile.name},
             {"detection_sha256", "a431985659dc921974177a95adcfbb90fd9e51989a5e04d70d0b75f597b6e61d"},
             {"recognition_sha256", "da72dc72ca4dc220df0dfde68c1dedc31c58d3e76a25871122e5056227d50092"},
             {"dictionary_sha256", "d1979e9f794c464c0d2e0b70a7fe14dd978e9dc644c0e71f14158cdf8342af1b"},
         }},
        {"config",
         {
             {"detection_limit_side", config.detection_limit_side},
             {"detection_threshold", config.detection_threshold},
             {"box_threshold", config.box_threshold},
             {"recognition_threshold", config.recognition_threshold},
         }},
    };
    predictions["samples"] = nlohmann::json::array();

    for (const nlohmann::json& sample : ground_truth.value("samples", nlohmann::json::array())) {
        doc_parser::document::PageArtifact page;
        page.page_index = static_cast<int>(predictions["samples"].size());
        page.page_number = page.page_index + 1;
        page.width = sample.value("width", 0);
        page.height = sample.value("height", 0);
        page.output_path = corpus_root / sample.value("image", "");

        doc_parser::ocr::OcrResult result;
        if (!backend.recognize({page, 200}, result)) {
            std::cerr << "PaddleOCR inference failed for " << page.output_path << '\n';
            return 1;
        }

        const std::string text = pageText(result.page_text);
        predictions["samples"].push_back({
            {"id", sample.at("id")},
            {"image", sample.value("image", "")},
            {"text", text},
        });
        std::cout << sample.at("id") << " lines=" << result.page_text.lines.size() << " chars=" << text.size() << '\n';
    }

    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream output(output_path);
    output << predictions.dump(2) << '\n';
    return output ? 0 : 1;
}
