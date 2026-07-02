#include "image/image_preprocessor.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace doc_parser::image {

cv::Mat ImagePreprocessor::preprocess(const cv::Mat& input, const PreprocessOptions& options) const {
    if (input.empty()) {
        return {};
    }

    cv::Mat result = toGrayscale(input);
    if (options.denoise) {
        result = denoise(result);
    }
    if (options.binarize) {
        result = binarize(result);
    }
    return result;
}

bool ImagePreprocessor::preprocessFile(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const PreprocessOptions& options
) const {
    const cv::Mat input = cv::imread(input_path.string(), cv::IMREAD_COLOR);
    const cv::Mat output = preprocess(input, options);
    if (output.empty()) {
        return false;
    }
    return cv::imwrite(output_path.string(), output);
}

cv::Mat ImagePreprocessor::toGrayscale(const cv::Mat& input) const {
    if (input.channels() == 1) {
        return input.clone();
    }

    cv::Mat grayscale;
    cv::cvtColor(input, grayscale, cv::COLOR_BGR2GRAY);
    return grayscale;
}

cv::Mat ImagePreprocessor::denoise(const cv::Mat& grayscale) const {
    cv::Mat output;
    cv::GaussianBlur(grayscale, output, cv::Size(3, 3), 0.0);
    return output;
}

cv::Mat ImagePreprocessor::binarize(const cv::Mat& grayscale) const {
    cv::Mat output;
    cv::threshold(grayscale, output, 0.0, 255.0, cv::THRESH_BINARY | cv::THRESH_OTSU);
    return output;
}

}  // namespace doc_parser::image
