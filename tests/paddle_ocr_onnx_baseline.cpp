#include "ocr/paddle_ocr_onnx_backend.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr int kSkip = 77;

std::string envString(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return {};
    }
    return value;
}

} // namespace

int main() {
    const doc_parser::ocr::PaddleOcrOnnxBackend backend;
    if (!backend.isAvailable()) {
        std::cout << "PaddleOCR ONNX baseline skipped; default models are unavailable. "
                     "Run bash scripts/setup_paddleocr_baseline.sh or set "
                     "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_MODEL_DIR.\n";
        return kSkip;
    }

    const std::string image_path = envString("DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_TEST_IMAGE");
    if (image_path.empty()) {
        return 0;
    }

    doc_parser::document::PageArtifact page;
    page.page_index = 0;
    page.page_number = 1;
    page.output_path = std::filesystem::path(image_path);

    doc_parser::ocr::OcrResult result;
    if (!backend.recognize({page, 200}, result)) {
        std::cerr << "PaddleOCR ONNX backend failed to recognize configured test image\n";
        return 1;
    }

    const std::string expected_text = envString("DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_EXPECT_TEXT");
    if (!expected_text.empty() && result.page_text.lines.empty()) {
        std::cerr << "PaddleOCR ONNX backend returned no text for expected text: " << expected_text << '\n';
        return 1;
    }

    if (!expected_text.empty()) {
        std::string full_text;
        for (const auto& line : result.page_text.lines) {
            full_text += line.text;
            full_text += '\n';
        }
        if (full_text.find(expected_text) == std::string::npos) {
            std::cerr << "PaddleOCR ONNX backend output did not contain expected text: " << expected_text << '\n';
            return 1;
        }
    }

    return 0;
}
