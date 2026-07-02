#pragma once

#include <filesystem>
#include <opencv2/core.hpp>

namespace doc_parser::image {

struct PreprocessOptions {
    bool denoise = true;
    bool binarize = true;
};

// Page image preprocessing for OCR/layout input. The first implementation keeps
// the pipeline conservative: grayscale, optional denoise, and optional Otsu
// binarization. Deskew and morphology can be added once OCR fixtures define the
// expected output behavior.
class ImagePreprocessor {
public:
    cv::Mat preprocess(const cv::Mat& input, const PreprocessOptions& options = {}) const;

    bool preprocessFile(const std::filesystem::path& input_path,
                        const std::filesystem::path& output_path,
                        const PreprocessOptions& options = {}) const;

private:
    cv::Mat toGrayscale(const cv::Mat& input) const;
    cv::Mat denoise(const cv::Mat& grayscale) const;
    cv::Mat binarize(const cv::Mat& grayscale) const;
};

} // namespace doc_parser::image
