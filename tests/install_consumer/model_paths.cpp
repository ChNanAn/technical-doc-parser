#include <document_intelligence_engine/document_engine.h>
#include <document_intelligence_engine/engine_config.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

bool isInside(const std::filesystem::path& path, const std::filesystem::path& root) {
    std::error_code error;
    const std::filesystem::path resolved_path = std::filesystem::weakly_canonical(path, error);
    if (error) {
        return false;
    }
    const std::filesystem::path resolved_root = std::filesystem::weakly_canonical(root, error);
    if (error) {
        return false;
    }
    return resolved_path == resolved_root || resolved_path.string().rfind(resolved_root.string() + '/', 0) == 0;
}

} // namespace

int main() {
    static_assert(!std::is_default_constructible_v<doc_parser::pipeline::DocumentEngine>);
    const doc_parser::pipeline::EngineConfig config = doc_parser::pipeline::defaultEngineConfig();
    const std::filesystem::path model_root = EXPECTED_MODEL_DIR;
    const std::vector<std::filesystem::path> model_files{
        config.paddle_ocr.detection_model,
        config.paddle_ocr.recognition_model,
        config.paddle_ocr.character_dict,
        config.doclaynet.model_path,
        config.paddle_layout.model_path,
        config.table_transformer.detection_model_path,
        config.table_transformer.structure_model_path,
    };

    for (const std::filesystem::path& model_file : model_files) {
        if (!isInside(model_file, model_root)) {
            std::cerr << "default model path escapes relocated package: " << model_file << '\n';
            return 1;
        }
#if EXPECT_INSTALLED_MODELS
        if (!std::filesystem::is_regular_file(model_file)) {
            std::cerr << "installed default model is missing: " << model_file << '\n';
            return 1;
        }
#else
        if (std::filesystem::exists(model_file)) {
            std::cerr << "default SDK install unexpectedly contains a model: " << model_file << '\n';
            return 1;
        }
#endif
    }

#if EXPECT_INSTALLED_MODELS
    doc_parser::pipeline::EngineConfig engine_config = config;
    engine_config.backends = {"pdf", "paddle", "text", "text", {}};
    const doc_parser::pipeline::DocumentEngine engine(std::move(engine_config));
    if (!engine.isReady()) {
        std::cerr << "public SDK cannot load installed PaddleOCR models: "
                  << engine.initializationStatus().message() << '\n';
        return 1;
    }
#endif

    std::cout << model_root << '\n';
    return 0;
}
