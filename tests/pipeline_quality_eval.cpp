#include "pipeline/backend_registry.h"
#include "pipeline/document_engine_internal.h"
#include "document_intelligence_engine/document_engine.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace {

struct Options {
    std::filesystem::path manifest;
    std::filesystem::path input_root;
    std::filesystem::path work_directory;
    std::filesystem::path output;
};

bool parseArgs(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (index + 1 >= argc) {
            return false;
        }
        const std::filesystem::path value = argv[++index];
        if (argument == "--manifest") {
            options.manifest = value;
        } else if (argument == "--input-root") {
            options.input_root = value;
        } else if (argument == "--work-dir") {
            options.work_directory = value;
        } else if (argument == "--output") {
            options.output = value;
        } else {
            return false;
        }
    }
    return !options.manifest.empty() && !options.input_root.empty() && !options.work_directory.empty() &&
           !options.output.empty();
}

const char* blockType(doc_parser::document::DocumentBlockType type) {
    switch (type) {
    case doc_parser::document::DocumentBlockType::Title:
        return "title";
    case doc_parser::document::DocumentBlockType::Paragraph:
        return "paragraph";
    case doc_parser::document::DocumentBlockType::List:
        return "list";
    case doc_parser::document::DocumentBlockType::Table:
        return "table";
    case doc_parser::document::DocumentBlockType::Figure:
        return "figure";
    case doc_parser::document::DocumentBlockType::Header:
        return "header";
    case doc_parser::document::DocumentBlockType::Footer:
        return "footer";
    case doc_parser::document::DocumentBlockType::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* layoutLabel(doc_parser::document::DocumentBlockType type) {
    return type == doc_parser::document::DocumentBlockType::Paragraph ? "text" : blockType(type);
}

nlohmann::json backendPolicy(const doc_parser::pipeline::EngineConfig& config,
                             const doc_parser::pipeline::BackendRegistryConfig& policy) {
    return {
        {"registry_source",
         config.backends.registry_config.empty() ? "builtin" : config.backends.registry_config.generic_string()},
        {"requested",
         {
             {"document", config.backends.document},
             {"ocr", config.backends.ocr},
             {"layout", config.backends.layout},
             {"table", config.backends.table},
         }},
        {"auto_order",
         {
             {"document", policy.document_auto_order},
             {"ocr", policy.ocr_auto_order},
             {"layout", policy.layout_auto_order},
             {"table", policy.table_auto_order},
         }},
    };
}

nlohmann::json modelPolicy(const doc_parser::pipeline::EngineConfig& config) {
    return {
        {"paddle_ocr",
         {
             {"profile", config.paddle_ocr.profile.name},
             {"detection_model", config.paddle_ocr.detection_model.generic_string()},
             {"recognition_model", config.paddle_ocr.recognition_model.generic_string()},
             {"character_dict", config.paddle_ocr.character_dict.generic_string()},
             {"detection_limit_side", config.paddle_ocr.detection_limit_side},
             {"detection_threshold", config.paddle_ocr.detection_threshold},
             {"box_threshold", config.paddle_ocr.box_threshold},
             {"recognition_threshold", config.paddle_ocr.recognition_threshold},
             {"recognition_batch_size", config.paddle_ocr.recognition_batch_size},
         }},
        {"doclaynet",
         {
             {"model", config.doclaynet.model_path.generic_string()},
             {"input_size", {config.doclaynet.input_width, config.doclaynet.input_height}},
             {"confidence_threshold", config.doclaynet.confidence_threshold},
         }},
        {"paddle_layout",
         {
             {"model", config.paddle_layout.model_path.generic_string()},
             {"input_size", {config.paddle_layout.input_width, config.paddle_layout.input_height}},
             {"confidence_threshold", config.paddle_layout.confidence_threshold},
         }},
        {"table_transformer",
         {
             {"detection_model", config.table_transformer.detection_model_path.generic_string()},
             {"structure_model", config.table_transformer.structure_model_path.generic_string()},
             {"detection_confidence_threshold", config.table_transformer.detection_confidence_threshold},
             {"structure_confidence_threshold", config.table_transformer.structure_confidence_threshold},
             {"crop_padding", config.table_transformer.crop_padding},
         }},
    };
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        std::cerr << "Usage: " << argv[0]
                  << " --manifest prepared_manifest.json --input-root inputs --work-dir work --output "
                     "predictions.json\n";
        return 2;
    }

    std::ifstream manifest_input(options.manifest);
    if (!manifest_input) {
        std::cerr << "Failed to read " << options.manifest << '\n';
        return 2;
    }
    const nlohmann::json manifest = nlohmann::json::parse(manifest_input);
    const std::string task = manifest.value("task", "pipeline_text_order");
    if (task != "pipeline_text_order" && task != "layout") {
        std::cerr << "Unsupported prediction task: " << task << '\n';
        return 2;
    }

    doc_parser::pipeline::EngineConfig config = doc_parser::pipeline::defaultEngineConfig();
    config.backends.document = "pdf";
    config.backends.ocr = "auto";
    config.backends.layout = "auto";
    config.backends.table = "auto";
    const doc_parser::pipeline::BackendRegistry registry = doc_parser::pipeline::createDefaultBackendRegistry(config);
    const doc_parser::pipeline::BackendRegistryConfigResult policy =
        doc_parser::pipeline::loadBackendRegistryConfig(config.backends.registry_config, registry);
    if (!policy.ok) {
        std::cerr << "Backend policy initialization failed: " << policy.error << '\n';
        return 1;
    }
    doc_parser::pipeline::DocumentEngine engine =
        doc_parser::pipeline::DocumentEngineInternalAccess::create(config, registry);
    if (!engine.isReady()) {
        std::cerr << "Document engine initialization failed: " << engine.initializationStatus().message() << '\n';
        return 1;
    }

    nlohmann::json predictions;
    predictions["version"] = 1;
    predictions["task"] = task;
    predictions["dataset"] = manifest.value("dataset", "technical-doc-parser/quality-baseline-15");
    predictions["metadata"] = {
        {"engine", "DocumentIntelligenceEngine"},
        {"output_scope", task == "layout" ? "final_document_blocks" : "final_document_text"},
        {"dpi", manifest.value("dpi", 200)},
        {"backend_policy", backendPolicy(config, policy.config)},
        {"model_policy", modelPolicy(config)},
    };
    predictions["samples"] = nlohmann::json::array();

    std::size_t parsed_pages = 0;
    for (const nlohmann::json& document : manifest.value("documents", nlohmann::json::array())) {
        const std::string document_id = document.at("id").get<std::string>();
        doc_parser::pipeline::DocumentParseOptions run_options;
        run_options.input_path = options.input_root / "pdf" / document.at("pdf").get<std::string>();
        run_options.output_directory = options.work_directory / document_id;
        run_options.render.dpi = manifest.value("dpi", 200);

        doc_parser::pipeline::ParseResult result = engine.parse(run_options);
        if (!result.ok()) {
            std::cerr << "Parsing " << document_id << " failed at " << result.status.stage() << " ["
                      << result.status.code() << "]: " << result.status.message() << '\n';
            return 1;
        }

        const auto pages = document.value("pages", nlohmann::json::array());
        if (result.artifacts.pages.size() != pages.size()) {
            std::cerr << "Parsed page count mismatch for " << document_id << '\n';
            return 1;
        }
        for (std::size_t page_index = 0; page_index < pages.size(); ++page_index) {
            nlohmann::json blocks = nlohmann::json::array();
            for (const doc_parser::document::DocumentBlock& block : result.document.blocks) {
                if (block.page_index != static_cast<int>(page_index)) {
                    continue;
                }
                blocks.push_back({
                    {"id", block.id},
                    {"type", blockType(block.type)},
                    {"mapped_label", layoutLabel(block.type)},
                    {"bbox", {block.bbox.x0, block.bbox.y0, block.bbox.x1, block.bbox.y1}},
                    {"text", block.text},
                });
            }
            const nlohmann::json sample_id =
                pages[page_index].contains("sample_id")
                    ? pages[page_index]["sample_id"]
                    : nlohmann::json(document_id + ":p" + (page_index + 1 < 10 ? "0" : "") +
                                     std::to_string(page_index + 1));
            nlohmann::json prediction_sample = {
                {"id", sample_id},
                {"document_id", document_id},
                {"page_number", page_index + 1},
                {"image_sha256", pages[page_index].value("sha256", "")},
                {"blocks", blocks},
            };
            if (task == "layout") {
                prediction_sample["objects"] = blocks;
            }
            predictions["samples"].push_back(std::move(prediction_sample));
            ++parsed_pages;
            std::cout << sample_id.dump() << " blocks=" << blocks.size() << '\n';
        }
    }

    if (parsed_pages != manifest.value("page_count", 0U)) {
        std::cerr << "Parsed " << parsed_pages << " pages but manifest expected " << manifest.value("page_count", 0U)
                  << '\n';
        return 1;
    }
    if (!options.output.parent_path().empty()) {
        std::filesystem::create_directories(options.output.parent_path());
    }
    std::ofstream output(options.output);
    output << predictions.dump(2) << '\n';
    return output ? 0 : 1;
}
