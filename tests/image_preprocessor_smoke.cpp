#include "image/image_preprocessor.h"

#include <filesystem>
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

int main() {
    cv::Mat input(8, 8, CV_8UC3);
    for (int y = 0; y < input.rows; ++y) {
        for (int x = 0; x < input.cols; ++x) {
            input.at<cv::Vec3b>(y, x) = cv::Vec3b(static_cast<unsigned char>(x * 20),
                                                  static_cast<unsigned char>(y * 20),
                                                  static_cast<unsigned char>((x + y) * 10));
        }
    }

    const doc_parser::image::ImagePreprocessor preprocessor;
    const cv::Mat output = preprocessor.preprocess(input);
    if (output.empty()) {
        std::cerr << "expected non-empty preprocessed image\n";
        return 1;
    }
    if (output.channels() != 1) {
        std::cerr << "expected single-channel preprocessed image\n";
        return 1;
    }
    if (output.rows != input.rows || output.cols != input.cols) {
        std::cerr << "expected preprocessed image to preserve size\n";
        return 1;
    }

    const std::filesystem::path input_path = "/tmp/technical-doc-parser-image-smoke/input.png";
    const std::filesystem::path output_path = "/tmp/technical-doc-parser-image-smoke/nested/output.png";
    std::filesystem::create_directories(input_path.parent_path());
    if (!cv::imwrite(input_path.string(), input)) {
        std::cerr << "failed to write input fixture image\n";
        return 1;
    }
    if (!preprocessor.preprocessFile(input_path, output_path)) {
        std::cerr << "failed to preprocess image file\n";
        return 1;
    }
    if (!std::filesystem::exists(output_path)) {
        std::cerr << "expected preprocessed output file\n";
        return 1;
    }

    return 0;
}
