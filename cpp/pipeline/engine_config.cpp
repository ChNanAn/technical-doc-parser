#include "pipeline/engine_config.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace doc_parser::pipeline {
namespace {

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

std::string firstEnvironment(const char* primary, const char* legacy = nullptr) {
    std::string value = environment(primary);
    if (value.empty() && legacy != nullptr) {
        value = environment(legacy);
    }
    return value;
}

void applyPath(const char* name, std::filesystem::path& target) {
    const std::string value = environment(name);
    if (!value.empty()) {
        target = value;
    }
}

void applyPositiveInt(const char* name, int& target) {
    const std::string value = environment(name);
    if (value.empty()) {
        return;
    }
    try {
        const int parsed = std::stoi(value);
        if (parsed > 0) {
            target = parsed;
        }
    } catch (const std::exception&) {
    }
}

void applyNonNegativeInt(const char* name, int& target) {
    const std::string value = environment(name);
    if (value.empty()) {
        return;
    }
    try {
        const int parsed = std::stoi(value);
        if (parsed >= 0) {
            target = parsed;
        }
    } catch (const std::exception&) {
    }
}

void applyPositiveSize(const char* name, std::size_t& target) {
    int parsed = static_cast<int>(target);
    applyPositiveInt(name, parsed);
    target = static_cast<std::size_t>(parsed);
}

void applyProbability(const char* name, double& target) {
    const std::string value = environment(name);
    if (value.empty()) {
        return;
    }
    try {
        const double parsed = std::stod(value);
        if (parsed > 0.0 && parsed < 1.0) {
            target = parsed;
        }
    } catch (const std::exception&) {
    }
}

} // namespace

EngineConfig engineConfigFromEnvironment(EngineConfig config) {
    if (config.backends.registry_config.empty()) {
        applyPath("DOCUMENT_INTELLIGENCE_ENGINE_BACKEND_CONFIG", config.backends.registry_config);
    }

    const std::string tesseract_executable =
        firstEnvironment("DOCUMENT_INTELLIGENCE_ENGINE_TESSERACT_CMD", "DOC_PARSER_TESSERACT_CMD");
    const std::string tesseract_language =
        firstEnvironment("DOCUMENT_INTELLIGENCE_ENGINE_TESSERACT_LANG", "DOC_PARSER_TESSERACT_LANG");
    if (!tesseract_executable.empty()) {
        config.tesseract.executable = tesseract_executable;
    }
    if (!tesseract_language.empty()) {
        config.tesseract.language = tesseract_language;
    }

    const std::string paddle_ocr_dir = environment("DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_MODEL_DIR");
    if (!paddle_ocr_dir.empty()) {
        config.paddle_ocr.detection_model = std::filesystem::path(paddle_ocr_dir) / "det.onnx";
        config.paddle_ocr.recognition_model = std::filesystem::path(paddle_ocr_dir) / "rec.onnx";
        config.paddle_ocr.character_dict = std::filesystem::path(paddle_ocr_dir) / "ppocrv5_dict.txt";
    }
    applyPath("DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DET_MODEL", config.paddle_ocr.detection_model);
    applyPath("DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_REC_MODEL", config.paddle_ocr.recognition_model);
    applyPath("DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DICT", config.paddle_ocr.character_dict);
    const std::string paddle_profile = environment("DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_PROFILE");
    if (!paddle_profile.empty()) {
        config.paddle_ocr.profile.name = paddle_profile;
    }
    applyPositiveSize("DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_REC_BATCH_SIZE",
                      config.paddle_ocr.recognition_batch_size);
    applyPositiveInt("DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_REC_MAX_WIDTH", config.paddle_ocr.recognition_max_width);
    applyPositiveInt("DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DET_LIMIT_SIDE", config.paddle_ocr.detection_limit_side);

    applyPath("DOCUMENT_INTELLIGENCE_ENGINE_DOCLAYNET_MODEL", config.doclaynet.model_path);
    applyProbability("DOCUMENT_INTELLIGENCE_ENGINE_DOCLAYNET_CONFIDENCE", config.doclaynet.confidence_threshold);
    applyPath("DOCUMENT_INTELLIGENCE_ENGINE_PADDLE_LAYOUT_MODEL", config.paddle_layout.model_path);
    applyProbability("DOCUMENT_INTELLIGENCE_ENGINE_PADDLE_LAYOUT_CONFIDENCE",
                     config.paddle_layout.confidence_threshold);

    applyPath("DOCUMENT_INTELLIGENCE_ENGINE_TABLE_DETECTION_MODEL", config.table_transformer.detection_model_path);
    applyPath("DOCUMENT_INTELLIGENCE_ENGINE_TABLE_STRUCTURE_MODEL", config.table_transformer.structure_model_path);
    applyProbability("DOCUMENT_INTELLIGENCE_ENGINE_TABLE_DETECTION_CONFIDENCE",
                     config.table_transformer.detection_confidence_threshold);
    applyProbability("DOCUMENT_INTELLIGENCE_ENGINE_TABLE_STRUCTURE_CONFIDENCE",
                     config.table_transformer.structure_confidence_threshold);
    applyNonNegativeInt("DOCUMENT_INTELLIGENCE_ENGINE_TABLE_CROP_PADDING", config.table_transformer.crop_padding);
    return config;
}

} // namespace doc_parser::pipeline
