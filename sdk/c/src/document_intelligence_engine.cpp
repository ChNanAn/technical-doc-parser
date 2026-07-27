#include "document_intelligence_engine/c_api.h"

#include "document_intelligence_engine/document_engine.h"
#include "export/json_document_exporter.h"

#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#ifndef DIE_ENGINE_VERSION
#define DIE_ENGINE_VERSION "unknown"
#endif

struct die_engine {
    explicit die_engine(doc_parser::pipeline::EngineConfig config) : value(std::move(config)) {}

    doc_parser::pipeline::DocumentEngine value;
};

struct die_document {
    std::string json;
};

struct die_error {
    die_result_t result = DIE_RESULT_INTERNAL_ERROR;
    std::string code;
    std::string message;
    std::string stage;
    bool retryable = false;
};

namespace {

using Json = nlohmann::json;

class InputError final : public std::runtime_error {
public:
    InputError(std::string error_code, std::string error_stage, std::string message)
        : std::runtime_error(std::move(message)), code(std::move(error_code)), stage(std::move(error_stage)) {}

    std::string code;
    std::string stage;
};

void clearError(die_error_t** out_error) {
    if (out_error != nullptr) {
        *out_error = nullptr;
    }
}

void setErrorNoexcept(die_error_t** out_error,
                      die_result_t result,
                      const std::string& code,
                      const std::string& message,
                      const std::string& stage,
                      bool retryable) noexcept {
    if (out_error == nullptr) {
        return;
    }
    try {
        auto error = std::make_unique<die_error>();
        error->result = result;
        error->code = code;
        error->message = message;
        error->stage = stage;
        error->retryable = retryable;
        *out_error = error.release();
    } catch (...) {
        *out_error = nullptr;
    }
}

die_result_t fail(die_error_t** out_error,
                  die_result_t result,
                  const std::string& code,
                  const std::string& message,
                  const std::string& stage,
                  bool retryable = false) {
    setErrorNoexcept(out_error, result, code, message, stage, retryable);
    return result;
}

die_result_t fail(die_error_t** out_error, die_result_t result, const doc_parser::common::Status& status) {
    return fail(
        out_error, result, status.code(), status.message(), status.stage(), status.retryable());
}

Json parseObject(const char* text, const std::string& code, const std::string& stage, const std::string& label) {
    if (text == nullptr || *text == '\0') {
        throw InputError(code, stage, label + " JSON must be a non-empty UTF-8 string");
    }
    try {
        Json value = Json::parse(text);
        if (!value.is_object()) {
            throw InputError(code, stage, label + " JSON root must be an object");
        }
        return value;
    } catch (const InputError&) {
        throw;
    } catch (const Json::exception& error) {
        throw InputError(code, stage, "invalid " + label + " JSON: " + std::string(error.what()));
    }
}

void rejectUnknown(const Json& object,
                   const std::set<std::string>& allowed,
                   const std::string& context,
                   const std::string& code,
                   const std::string& stage) {
    if (!object.is_object()) {
        throw InputError(code, stage, context + " must be an object");
    }
    for (auto item = object.begin(); item != object.end(); ++item) {
        if (allowed.find(item.key()) == allowed.end()) {
            throw InputError(code, stage, context + " contains unknown field '" + item.key() + "'");
        }
    }
}

const Json* optionalObject(const Json& parent,
                           const char* key,
                           const std::set<std::string>& allowed,
                           const std::string& code,
                           const std::string& stage) {
    const auto item = parent.find(key);
    if (item == parent.end()) {
        return nullptr;
    }
    rejectUnknown(*item, allowed, key, code, stage);
    return &*item;
}

void requireSchemaV1(const Json& object, const std::string& code, const std::string& stage) {
    const auto item = object.find("schema_version");
    if (item == object.end() || !item->is_number_integer() || *item != 1) {
        throw InputError(code, stage, "schema_version must be integer 1");
    }
}

std::string stringValue(const Json& object,
                        const char* key,
                        const std::string& code,
                        const std::string& stage,
                        bool required = false,
                        bool allow_empty = false) {
    const auto item = object.find(key);
    if (item == object.end()) {
        if (required) {
            throw InputError(code, stage, std::string(key) + " is required");
        }
        return {};
    }
    if (!item->is_string()) {
        throw InputError(code, stage, std::string(key) + " must be a string");
    }
    std::string value = item->get<std::string>();
    if (value.find('\0') != std::string::npos) {
        throw InputError(code, stage, std::string(key) + " must not contain NUL");
    }
    if (!allow_empty && value.empty()) {
        throw InputError(code, stage, std::string(key) + " must not be empty");
    }
    return value;
}

void applyString(const Json& object,
                 const char* key,
                 std::string& target,
                 const std::string& code,
                 const std::string& stage,
                 bool allow_empty = false) {
    if (object.contains(key)) {
        target = std::filesystem::u8path(
            stringValue(object, key, code, stage, true, allow_empty));
    }
}

void applyPath(const Json& object,
               const char* key,
               std::filesystem::path& target,
               const std::string& code,
               const std::string& stage,
               bool allow_empty = false) {
    if (object.contains(key)) {
        target = stringValue(object, key, code, stage, true, allow_empty);
    }
}

int integerValue(const Json& object,
                 const char* key,
                 const std::string& code,
                 const std::string& stage,
                 int minimum,
                 bool required = false) {
    const auto item = object.find(key);
    if (item == object.end()) {
        if (required) {
            throw InputError(code, stage, std::string(key) + " is required");
        }
        return minimum;
    }
    if (!item->is_number_integer()) {
        throw InputError(code, stage, std::string(key) + " must be an integer");
    }
    bool in_range = false;
    int value = 0;
    if (item->is_number_unsigned()) {
        const unsigned long long parsed = item->get<unsigned long long>();
        in_range = parsed >= static_cast<unsigned long long>(minimum) &&
                   parsed <= static_cast<unsigned long long>(std::numeric_limits<int>::max());
        if (in_range) {
            value = static_cast<int>(parsed);
        }
    } else {
        const long long parsed = item->get<long long>();
        in_range = parsed >= minimum && parsed <= std::numeric_limits<int>::max();
        if (in_range) {
            value = static_cast<int>(parsed);
        }
    }
    if (!in_range) {
        throw InputError(code,
                         stage,
                         std::string(key) + " must be between " + std::to_string(minimum) + " and " +
                             std::to_string(std::numeric_limits<int>::max()));
    }
    return value;
}

void applyInteger(const Json& object,
                  const char* key,
                  int& target,
                  const std::string& code,
                  const std::string& stage,
                  int minimum) {
    if (object.contains(key)) {
        target = integerValue(object, key, code, stage, minimum, true);
    }
}

void applySize(const Json& object,
               const char* key,
               std::size_t& target,
               const std::string& code,
               const std::string& stage,
               std::size_t minimum) {
    const auto item = object.find(key);
    if (item == object.end()) {
        return;
    }
    if (!item->is_number_unsigned() && !item->is_number_integer()) {
        throw InputError(code, stage, std::string(key) + " must be an integer");
    }
    unsigned long long value = 0;
    if (item->is_number_unsigned()) {
        value = item->get<unsigned long long>();
    } else {
        const long long parsed = item->get<long long>();
        if (parsed < 0) {
            throw InputError(
                code, stage, std::string(key) + " must be at least " + std::to_string(minimum));
        }
        value = static_cast<unsigned long long>(parsed);
    }
    if (value < minimum || value > std::numeric_limits<std::size_t>::max()) {
        throw InputError(
            code, stage, std::string(key) + " must be at least " + std::to_string(minimum));
    }
    target = static_cast<std::size_t>(value);
}

double numberValue(const Json& object,
                   const char* key,
                   const std::string& code,
                   const std::string& stage,
                   double minimum,
                   double maximum,
                   bool include_minimum = true) {
    const auto item = object.find(key);
    if (item == object.end() || !item->is_number()) {
        throw InputError(code, stage, std::string(key) + " must be a number");
    }
    const double value = item->get<double>();
    const bool below_minimum = include_minimum ? value < minimum : value <= minimum;
    if (!std::isfinite(value) || below_minimum || value > maximum) {
        throw InputError(code,
                         stage,
                         std::string(key) + " must be in the supported range");
    }
    return value;
}

void applyProbability(const Json& object,
                      const char* key,
                      double& target,
                      const std::string& code,
                      const std::string& stage) {
    if (object.contains(key)) {
        target = numberValue(object, key, code, stage, 0.0, 1.0);
    }
}

doc_parser::pipeline::EngineConfig engineConfig(const char* config_json) {
    constexpr const char* code = "c_api.invalid_config";
    constexpr const char* stage = "configure";
    const Json root = parseObject(config_json, code, stage, "engine configuration");
    rejectUnknown(root,
                  {"schema_version", "backends", "tesseract", "models"},
                  "engine configuration",
                  code,
                  stage);
    requireSchemaV1(root, code, stage);

    doc_parser::pipeline::EngineConfig config = doc_parser::pipeline::defaultEngineConfig();
    if (const Json* backends = optionalObject(
            root, "backends", {"document", "ocr", "layout", "table", "registry_config"}, code, stage)) {
        applyString(*backends, "document", config.backends.document, code, stage);
        applyString(*backends, "ocr", config.backends.ocr, code, stage);
        applyString(*backends, "layout", config.backends.layout, code, stage);
        applyString(*backends, "table", config.backends.table, code, stage);
        applyPath(*backends, "registry_config", config.backends.registry_config, code, stage, true);
    }
    if (const Json* tesseract =
            optionalObject(root, "tesseract", {"executable", "language"}, code, stage)) {
        applyString(*tesseract, "executable", config.tesseract.executable, code, stage);
        applyString(*tesseract, "language", config.tesseract.language, code, stage);
    }
    const Json* models = optionalObject(
        root, "models", {"paddle_ocr", "doclaynet", "paddle_layout", "table_transformer"}, code, stage);
    if (models == nullptr) {
        return config;
    }

    if (const Json* paddle = optionalObject(
            *models,
            "paddle_ocr",
            {"detection_model",
             "recognition_model",
             "character_dict",
             "profile",
             "detection_limit_side",
             "recognition_image_height",
             "recognition_base_width",
             "recognition_max_width",
             "recognition_width_multiple",
             "recognition_batch_size",
             "detection_max_candidates",
             "detection_threshold",
             "box_threshold",
             "recognition_threshold",
             "unclip_ratio"},
            code,
            stage)) {
        applyPath(*paddle, "detection_model", config.paddle_ocr.detection_model, code, stage);
        applyPath(*paddle, "recognition_model", config.paddle_ocr.recognition_model, code, stage);
        applyPath(*paddle, "character_dict", config.paddle_ocr.character_dict, code, stage);
        applyString(*paddle, "profile", config.paddle_ocr.profile.name, code, stage);
        applyInteger(*paddle, "detection_limit_side", config.paddle_ocr.detection_limit_side, code, stage, 1);
        applyInteger(
            *paddle, "recognition_image_height", config.paddle_ocr.recognition_image_height, code, stage, 1);
        applyInteger(*paddle, "recognition_base_width", config.paddle_ocr.recognition_base_width, code, stage, 1);
        applyInteger(*paddle, "recognition_max_width", config.paddle_ocr.recognition_max_width, code, stage, 1);
        applyInteger(
            *paddle, "recognition_width_multiple", config.paddle_ocr.recognition_width_multiple, code, stage, 1);
        applySize(*paddle, "recognition_batch_size", config.paddle_ocr.recognition_batch_size, code, stage, 1);
        applySize(*paddle, "detection_max_candidates", config.paddle_ocr.detection_max_candidates, code, stage, 1);
        applyProbability(*paddle, "detection_threshold", config.paddle_ocr.detection_threshold, code, stage);
        applyProbability(*paddle, "box_threshold", config.paddle_ocr.box_threshold, code, stage);
        applyProbability(*paddle, "recognition_threshold", config.paddle_ocr.recognition_threshold, code, stage);
        if (paddle->contains("unclip_ratio")) {
            config.paddle_ocr.unclip_ratio =
                numberValue(*paddle,
                            "unclip_ratio",
                            code,
                            stage,
                            0.0,
                            std::numeric_limits<double>::max(),
                            false);
        }
    }
    if (const Json* doclaynet = optionalObject(
            *models, "doclaynet", {"model_path", "input_width", "input_height", "confidence_threshold"}, code, stage)) {
        applyPath(*doclaynet, "model_path", config.doclaynet.model_path, code, stage);
        applyInteger(*doclaynet, "input_width", config.doclaynet.input_width, code, stage, 1);
        applyInteger(*doclaynet, "input_height", config.doclaynet.input_height, code, stage, 1);
        applyProbability(*doclaynet, "confidence_threshold", config.doclaynet.confidence_threshold, code, stage);
    }
    if (const Json* paddle_layout =
            optionalObject(*models,
                           "paddle_layout",
                           {"model_path", "input_width", "input_height", "confidence_threshold"},
                           code,
                           stage)) {
        applyPath(*paddle_layout, "model_path", config.paddle_layout.model_path, code, stage);
        applyInteger(*paddle_layout, "input_width", config.paddle_layout.input_width, code, stage, 1);
        applyInteger(*paddle_layout, "input_height", config.paddle_layout.input_height, code, stage, 1);
        applyProbability(
            *paddle_layout, "confidence_threshold", config.paddle_layout.confidence_threshold, code, stage);
    }
    if (const Json* table =
            optionalObject(*models,
                           "table_transformer",
                           {"detection_model",
                            "structure_model",
                            "detection_confidence_threshold",
                            "structure_confidence_threshold",
                            "crop_padding"},
                           code,
                           stage)) {
        applyPath(
            *table, "detection_model", config.table_transformer.detection_model_path, code, stage);
        applyPath(
            *table, "structure_model", config.table_transformer.structure_model_path, code, stage);
        applyProbability(*table,
                         "detection_confidence_threshold",
                         config.table_transformer.detection_confidence_threshold,
                         code,
                         stage);
        applyProbability(*table,
                         "structure_confidence_threshold",
                         config.table_transformer.structure_confidence_threshold,
                         code,
                         stage);
        applyInteger(*table, "crop_padding", config.table_transformer.crop_padding, code, stage, 0);
    }
    return config;
}

doc_parser::pipeline::DocumentParseOptions parseOptions(const char* options_json) {
    constexpr const char* code = "c_api.invalid_options";
    constexpr const char* stage = "parse";
    const Json root = parseObject(options_json, code, stage, "parse options");
    rejectUnknown(root,
                  {"schema_version",
                   "input_path",
                   "output_directory",
                   "dpi",
                   "debug",
                   "timeout_seconds",
                   "maximum_pages",
                   "run_id"},
                  "parse options",
                  code,
                  stage);
    requireSchemaV1(root, code, stage);

    doc_parser::pipeline::DocumentParseOptions options;
    options.input_path =
        std::filesystem::u8path(stringValue(root, "input_path", code, stage, true));
    options.output_directory =
        std::filesystem::u8path(stringValue(root, "output_directory", code, stage, true));
    if (root.contains("dpi")) {
        options.render.dpi = integerValue(root, "dpi", code, stage, 1, true);
    }
    if (root.contains("debug")) {
        if (!root["debug"].is_boolean()) {
            throw InputError(code, stage, "debug must be a boolean");
        }
        options.debug = root["debug"].get<bool>();
    }
    if (root.contains("timeout_seconds")) {
        options.timeout_seconds = integerValue(root, "timeout_seconds", code, stage, 0, true);
    }
    if (root.contains("maximum_pages")) {
        options.maximum_pages = integerValue(root, "maximum_pages", code, stage, 0, true);
    }
    if (root.contains("run_id")) {
        options.run_id = stringValue(root, "run_id", code, stage, true, true);
    }
    return options;
}

die_engine_state_t engineState(doc_parser::pipeline::DocumentEngineState state) {
    switch (state) {
    case doc_parser::pipeline::DocumentEngineState::Ready:
        return DIE_ENGINE_STATE_READY;
    case doc_parser::pipeline::DocumentEngineState::Parsing:
        return DIE_ENGINE_STATE_PARSING;
    case doc_parser::pipeline::DocumentEngineState::InitializationFailed:
        return DIE_ENGINE_STATE_INITIALIZATION_FAILED;
    case doc_parser::pipeline::DocumentEngineState::MovedFrom:
        return DIE_ENGINE_STATE_MOVED_FROM;
    }
    return DIE_ENGINE_STATE_INVALID;
}

} // namespace

extern "C" {

uint32_t die_abi_version(void) { return DIE_ABI_VERSION; }

const char* die_engine_version(void) { return DIE_ENGINE_VERSION; }

die_result_t die_engine_create(const char* config_json, die_engine_t** out_engine, die_error_t** out_error) {
    if (out_engine != nullptr) {
        *out_engine = nullptr;
    }
    clearError(out_error);
    if (out_engine == nullptr) {
        return fail(out_error,
                    DIE_RESULT_INVALID_ARGUMENT,
                    "c_api.invalid_argument",
                    "out_engine must not be null",
                    "configure");
    }
    try {
        auto engine = std::make_unique<die_engine>(engineConfig(config_json));
        if (!engine->value.isReady()) {
            return fail(
                out_error, DIE_RESULT_CONFIGURATION_ERROR, engine->value.initializationStatus());
        }
        *out_engine = engine.release();
        return DIE_RESULT_OK;
    } catch (const InputError& error) {
        return fail(out_error, DIE_RESULT_INVALID_ARGUMENT, error.code, error.what(), error.stage);
    } catch (const std::exception& error) {
        return fail(out_error,
                    DIE_RESULT_INTERNAL_ERROR,
                    "c_api.internal_exception",
                    "engine creation failed: " + std::string(error.what()),
                    "configure");
    } catch (...) {
        return fail(out_error,
                    DIE_RESULT_INTERNAL_ERROR,
                    "c_api.internal_exception",
                    "engine creation failed with an unknown exception",
                    "configure");
    }
}

die_engine_state_t die_engine_get_state(const die_engine_t* engine) {
    if (engine == nullptr) {
        return DIE_ENGINE_STATE_INVALID;
    }
    try {
        return engineState(engine->value.state());
    } catch (...) {
        return DIE_ENGINE_STATE_INVALID;
    }
}

die_result_t die_engine_parse(die_engine_t* engine,
                              const char* options_json,
                              die_document_t** out_document,
                              die_error_t** out_error) {
    if (out_document != nullptr) {
        *out_document = nullptr;
    }
    clearError(out_error);
    if (engine == nullptr || out_document == nullptr) {
        return fail(out_error,
                    DIE_RESULT_INVALID_ARGUMENT,
                    "c_api.invalid_argument",
                    "engine and out_document must not be null",
                    "parse");
    }
    try {
        const doc_parser::pipeline::DocumentParseOptions options = parseOptions(options_json);
        doc_parser::pipeline::ParseResult parsed = engine->value.parse(options);
        if (!parsed.ok()) {
            return fail(out_error, DIE_RESULT_PARSE_ERROR, parsed.status);
        }
        doc_parser::exporter::JsonDocumentSerializationResult serialized =
            doc_parser::exporter::JsonDocumentExporter().serialize({
                options.debug,
                &parsed.document,
                &parsed.artifacts,
            });
        if (!serialized.ok()) {
            return fail(out_error, DIE_RESULT_SERIALIZATION_ERROR, serialized.status);
        }
        auto document = std::make_unique<die_document>();
        document->json = std::move(serialized.json);
        *out_document = document.release();
        return DIE_RESULT_OK;
    } catch (const InputError& error) {
        return fail(out_error, DIE_RESULT_INVALID_ARGUMENT, error.code, error.what(), error.stage);
    } catch (const std::exception& error) {
        return fail(out_error,
                    DIE_RESULT_INTERNAL_ERROR,
                    "c_api.internal_exception",
                    "document parsing failed: " + std::string(error.what()),
                    "parse");
    } catch (...) {
        return fail(out_error,
                    DIE_RESULT_INTERNAL_ERROR,
                    "c_api.internal_exception",
                    "document parsing failed with an unknown exception",
                    "parse");
    }
}

const char* die_document_json(const die_document_t* document) {
    return document == nullptr ? "" : document->json.c_str();
}

size_t die_document_json_size(const die_document_t* document) {
    return document == nullptr ? 0U : document->json.size();
}

die_result_t die_error_result(const die_error_t* error) {
    return error == nullptr ? DIE_RESULT_OK : error->result;
}

const char* die_error_code(const die_error_t* error) {
    return error == nullptr ? "" : error->code.c_str();
}

const char* die_error_message(const die_error_t* error) {
    return error == nullptr ? "" : error->message.c_str();
}

const char* die_error_stage(const die_error_t* error) {
    return error == nullptr ? "" : error->stage.c_str();
}

int die_error_retryable(const die_error_t* error) {
    return error != nullptr && error->retryable ? 1 : 0;
}

void die_document_destroy(die_document_t* document) {
    try {
        delete document;
    } catch (...) {
    }
}

void die_engine_destroy(die_engine_t* engine) {
    try {
        delete engine;
    } catch (...) {
    }
}

void die_error_destroy(die_error_t* error) {
    try {
        delete error;
    } catch (...) {
    }
}

} // extern "C"
