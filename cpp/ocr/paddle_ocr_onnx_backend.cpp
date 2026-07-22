#include "ocr/paddle_ocr_onnx_backend.h"

#include "document/text_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <onnxruntime_cxx_api.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace doc_parser::ocr {

struct PaddleOcrOnnxBackend::ModelBundle {
    ModelBundle() : env(ORT_LOGGING_LEVEL_WARNING, "document-intelligence-engine-paddleocr") {
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
    }

    std::unique_ptr<Ort::Session> createSession(const std::filesystem::path& model_path) {
        return std::make_unique<Ort::Session>(env, model_path.string().c_str(), session_options);
    }

    Ort::Env env;
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> detection_session;
    std::unique_ptr<Ort::Session> recognition_session;
    std::vector<std::string> detection_input_names;
    std::vector<std::string> detection_output_names;
    std::vector<std::string> recognition_input_names;
    std::vector<std::string> recognition_output_names;
    std::vector<int64_t> recognition_input_shape;
    std::vector<std::string> dictionary;
};

namespace {

constexpr const char* kDetectionModelEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DET_MODEL";
constexpr const char* kRecognitionModelEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_REC_MODEL";
constexpr const char* kCharacterDictEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DICT";
constexpr const char* kModelDirEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_MODEL_DIR";
constexpr const char* kProfileEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_PROFILE";
constexpr const char* kRecognitionBatchSizeEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_REC_BATCH_SIZE";
constexpr const char* kRecognitionMaxWidthEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_REC_MAX_WIDTH";
constexpr const char* kDetectionLimitSideEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DET_LIMIT_SIDE";
constexpr const char* kDebugEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DEBUG";

#ifndef DOC_PARSER_PADDLEOCR_BASELINE_DIR
#define DOC_PARSER_PADDLEOCR_BASELINE_DIR "models/paddleocr/baseline"
#endif

struct DetectionInput {
    std::vector<float> tensor;
    std::array<int64_t, 4> shape{};
};

struct RecognitionBatchInput {
    std::vector<float> tensor;
    std::array<int64_t, 4> shape{};
    std::vector<std::size_t> source_indices;
};

struct DetectionBox {
    std::array<cv::Point2f, 4> points{};
    document::BBox bbox;
    double score = 0.0;
};

struct RecognitionResult {
    std::string text;
    double confidence = 0.0;
};

struct DetectionStats {
    std::vector<int64_t> output_shape;
    double probability_min = 0.0;
    double probability_max = 0.0;
    std::size_t contours = 0;
    std::size_t skipped_small_contours = 0;
    std::size_t skipped_low_score = 0;
    std::size_t skipped_small_boxes = 0;
    std::size_t accepted_boxes = 0;
};

struct RecognitionStats {
    std::size_t crops = 0;
    std::size_t empty_crops = 0;
    std::size_t inference_runs = 0;
    std::size_t decoded_texts = 0;
    std::size_t empty_decodes = 0;
    std::vector<int64_t> first_output_shape;
};

std::filesystem::path envPath(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return {};
    }
    return std::filesystem::path(value);
}

std::string envString(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value == nullptr || std::string(value).empty() ? fallback : std::string(value);
}

int envPositiveInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return fallback;
    }
    try {
        const int parsed = std::stoi(value);
        return parsed > 0 ? parsed : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}

bool envFlag(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return false;
    }
    const std::string flag(value);
    return flag != "0" && flag != "false" && flag != "FALSE" && flag != "off" && flag != "OFF";
}

bool fileExists(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::exists(path, error);
}

std::vector<std::string> tensorNames(const Ort::Session& session, bool input) {
    Ort::AllocatorWithDefaultOptions allocator;
    const std::size_t count = input ? session.GetInputCount() : session.GetOutputCount();
    std::vector<std::string> names;
    names.reserve(count);

    for (std::size_t index = 0; index < count; ++index) {
        auto name = input ? session.GetInputNameAllocated(index, allocator)
                          : session.GetOutputNameAllocated(index, allocator);
        names.emplace_back(name.get());
    }

    return names;
}

std::vector<int64_t> inputShape(const Ort::Session& session, std::size_t index) {
    return session.GetInputTypeInfo(index).GetTensorTypeAndShapeInfo().GetShape();
}

std::vector<const char*> namePointers(const std::vector<std::string>& names) {
    std::vector<const char*> pointers;
    pointers.reserve(names.size());
    for (const std::string& name : names) {
        pointers.push_back(name.c_str());
    }
    return pointers;
}

std::string shapeToString(const std::vector<int64_t>& shape) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < shape.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }
        stream << shape[index];
    }
    stream << ']';
    return stream.str();
}

bool applyModelProfile(const std::string& name, PaddleOcrOnnxConfig& config) {
    if (name != "ppocrv4_mobile" && name != "ppocrv5_mobile") {
        return false;
    }

    config.profile = {};
    config.profile.name = name;
    config.profile.detection_mean = {0.485F, 0.456F, 0.406F};
    config.profile.detection_std = {0.229F, 0.224F, 0.225F};
    config.profile.detection_scale = 1.0F / 255.0F;
    config.profile.recognition_mean = {0.5F, 0.5F, 0.5F};
    config.profile.recognition_std = {0.5F, 0.5F, 0.5F};
    config.profile.recognition_scale = 1.0F / 255.0F;
    config.profile.convert_bgr_to_rgb = false;
    config.recognition_image_height = 48;
    config.recognition_base_width = 320;
    return true;
}

PaddleOcrOnnxConfig configFromEnvironment() {
    PaddleOcrOnnxConfig config;
    const std::string profile_name = envString(kProfileEnv, "ppocrv5_mobile");
    if (!applyModelProfile(profile_name, config)) {
        config.profile.name = profile_name;
    }
    std::filesystem::path model_dir = envPath(kModelDirEnv);
    if (model_dir.empty()) {
        model_dir = DOC_PARSER_PADDLEOCR_BASELINE_DIR;
    }

    config.detection_model = envPath(kDetectionModelEnv);
    if (config.detection_model.empty()) {
        config.detection_model = model_dir / "det.onnx";
    }

    config.recognition_model = envPath(kRecognitionModelEnv);
    if (config.recognition_model.empty()) {
        config.recognition_model = model_dir / "rec.onnx";
    }

    config.character_dict = envPath(kCharacterDictEnv);
    if (config.character_dict.empty()) {
        config.character_dict = model_dir / "ppocrv5_dict.txt";
    }

    config.recognition_batch_size = static_cast<std::size_t>(
        envPositiveInt(kRecognitionBatchSizeEnv, static_cast<int>(config.recognition_batch_size)));
    config.recognition_max_width = envPositiveInt(kRecognitionMaxWidthEnv, config.recognition_max_width);
    config.detection_limit_side = envPositiveInt(kDetectionLimitSideEnv, config.detection_limit_side);
    return config;
}

std::vector<std::string> loadDictionary(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<std::string> dictionary;
    std::string line;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            dictionary.push_back(line);
        }
    }

    return dictionary;
}

int roundToMultipleOf32(int value) {
    const int rounded = static_cast<int>(std::round(static_cast<double>(value) / 32.0)) * 32;
    return std::max(32, rounded);
}

int roundUpToMultiple(int value, int multiple) {
    if (multiple <= 1) {
        return value;
    }
    return ((value + multiple - 1) / multiple) * multiple;
}

DetectionInput makeDetectionInput(const cv::Mat& bgr_image, const PaddleOcrOnnxConfig& config) {
    const int original_width = bgr_image.cols;
    const int original_height = bgr_image.rows;
    const int max_side = std::max(original_width, original_height);
    const double ratio = max_side > config.detection_limit_side
                             ? static_cast<double>(config.detection_limit_side) / static_cast<double>(max_side)
                             : 1.0;
    const int resized_width = roundToMultipleOf32(static_cast<int>(std::round(original_width * ratio)));
    const int resized_height = roundToMultipleOf32(static_cast<int>(std::round(original_height * ratio)));

    cv::Mat resized;
    cv::resize(bgr_image, resized, cv::Size(resized_width, resized_height), 0.0, 0.0, cv::INTER_LINEAR);
    if (config.profile.convert_bgr_to_rgb) {
        cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
    }
    resized.convertTo(resized, CV_32FC3, config.profile.detection_scale);

    DetectionInput input;
    input.shape = {1, 3, resized_height, resized_width};
    input.tensor.resize(static_cast<std::size_t>(3 * resized_width * resized_height));

    for (int y = 0; y < resized_height; ++y) {
        for (int x = 0; x < resized_width; ++x) {
            const cv::Vec3f pixel = resized.at<cv::Vec3f>(y, x);
            for (int channel = 0; channel < 3; ++channel) {
                const std::size_t offset =
                    static_cast<std::size_t>(channel * resized_width * resized_height + y * resized_width + x);
                input.tensor[offset] =
                    (pixel[channel] - config.profile.detection_mean[channel]) / config.profile.detection_std[channel];
            }
        }
    }

    return input;
}

std::array<cv::Point2f, 4> orderedPoints(const std::array<cv::Point2f, 4>& points) {
    std::array<cv::Point2f, 4> ordered{};

    const auto min_sum =
        std::min_element(points.begin(), points.end(), [](const cv::Point2f& lhs, const cv::Point2f& rhs) {
            return lhs.x + lhs.y < rhs.x + rhs.y;
        });
    const auto max_sum =
        std::max_element(points.begin(), points.end(), [](const cv::Point2f& lhs, const cv::Point2f& rhs) {
            return lhs.x + lhs.y < rhs.x + rhs.y;
        });
    const auto min_diff =
        std::min_element(points.begin(), points.end(), [](const cv::Point2f& lhs, const cv::Point2f& rhs) {
            return lhs.y - lhs.x < rhs.y - rhs.x;
        });
    const auto max_diff =
        std::max_element(points.begin(), points.end(), [](const cv::Point2f& lhs, const cv::Point2f& rhs) {
            return lhs.y - lhs.x < rhs.y - rhs.x;
        });

    ordered[0] = *min_sum;
    ordered[1] = *min_diff;
    ordered[2] = *max_sum;
    ordered[3] = *max_diff;
    return ordered;
}

document::BBox bboxFromPoints(const std::array<cv::Point2f, 4>& points, const cv::Size& image_size) {
    document::BBox bbox{
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        0.0,
        0.0,
    };

    for (const cv::Point2f& point : points) {
        bbox.x0 = std::min(bbox.x0, static_cast<double>(point.x));
        bbox.y0 = std::min(bbox.y0, static_cast<double>(point.y));
        bbox.x1 = std::max(bbox.x1, static_cast<double>(point.x));
        bbox.y1 = std::max(bbox.y1, static_cast<double>(point.y));
    }

    bbox.x0 = std::clamp(bbox.x0, 0.0, static_cast<double>(image_size.width));
    bbox.y0 = std::clamp(bbox.y0, 0.0, static_cast<double>(image_size.height));
    bbox.x1 = std::clamp(bbox.x1, 0.0, static_cast<double>(image_size.width));
    bbox.y1 = std::clamp(bbox.y1, 0.0, static_cast<double>(image_size.height));
    return bbox;
}

double distance(const cv::Point2f& lhs, const cv::Point2f& rhs) {
    const double dx = static_cast<double>(lhs.x - rhs.x);
    const double dy = static_cast<double>(lhs.y - rhs.y);
    return std::sqrt(dx * dx + dy * dy);
}

double boxScoreFast(const cv::Mat& probability_map, const std::array<cv::Point2f, 4>& points) {
    const cv::Rect bounds = cv::boundingRect(std::vector<cv::Point2f>(points.begin(), points.end())) &
                            cv::Rect(0, 0, probability_map.cols, probability_map.rows);
    if (bounds.empty()) {
        return 0.0;
    }

    std::vector<cv::Point> local_points;
    local_points.reserve(points.size());
    for (const cv::Point2f& point : points) {
        local_points.emplace_back(static_cast<int>(std::round(point.x)) - bounds.x,
                                  static_cast<int>(std::round(point.y)) - bounds.y);
    }
    cv::Mat mask = cv::Mat::zeros(bounds.height, bounds.width, CV_8UC1);
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{local_points}, cv::Scalar(255));
    return cv::mean(probability_map(bounds), mask)[0];
}

cv::RotatedRect unclipRectangle(const cv::RotatedRect& rectangle, double unclip_ratio) {
    const double width = std::max(0.0F, rectangle.size.width);
    const double height = std::max(0.0F, rectangle.size.height);
    const double perimeter = 2.0 * (width + height);
    if (perimeter <= 0.0) {
        return rectangle;
    }

    const double offset = width * height * unclip_ratio / perimeter;
    cv::RotatedRect expanded = rectangle;
    expanded.size.width = static_cast<float>(width + 2.0 * offset);
    expanded.size.height = static_cast<float>(height + 2.0 * offset);
    return expanded;
}

std::vector<DetectionBox> extractBoxes(const Ort::Value& detection_output,
                                       const cv::Size& image_size,
                                       const PaddleOcrOnnxConfig& config,
                                       DetectionStats* stats) {
    const float* probabilities = detection_output.GetTensorData<float>();
    const auto shape = detection_output.GetTensorTypeAndShapeInfo().GetShape();
    if (stats != nullptr) {
        stats->output_shape = shape;
    }
    if (probabilities == nullptr || shape.size() < 2) {
        return {};
    }

    const int probability_width = static_cast<int>(shape.back());
    const int probability_height = static_cast<int>(shape[shape.size() - 2]);
    if (probability_width <= 0 || probability_height <= 0) {
        return {};
    }

    cv::Mat probability_map(probability_height, probability_width, CV_32FC1, const_cast<float*>(probabilities));
    if (stats != nullptr) {
        double min_value = 0.0;
        double max_value = 0.0;
        cv::minMaxLoc(probability_map, &min_value, &max_value);
        stats->probability_min = min_value;
        stats->probability_max = max_value;
    }

    cv::Mat bitmap;
    cv::threshold(probability_map, bitmap, config.detection_threshold, 255.0, cv::THRESH_BINARY);
    bitmap.convertTo(bitmap, CV_8UC1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    if (stats != nullptr) {
        stats->contours = contours.size();
    }

    std::vector<DetectionBox> boxes;
    const std::size_t candidate_count = std::min(contours.size(), config.detection_max_candidates);
    for (std::size_t contour_index = 0; contour_index < candidate_count; ++contour_index) {
        const auto& contour = contours[contour_index];
        if (contour.size() < 4) {
            if (stats != nullptr) {
                ++stats->skipped_small_contours;
            }
            continue;
        }

        cv::RotatedRect rectangle = cv::minAreaRect(contour);
        if (std::min(rectangle.size.width, rectangle.size.height) < 3.0F) {
            if (stats != nullptr) {
                ++stats->skipped_small_contours;
            }
            continue;
        }

        std::array<cv::Point2f, 4> points{};
        rectangle.points(points.data());
        points = orderedPoints(points);
        const double score = boxScoreFast(probability_map, points);
        if (score < config.box_threshold) {
            if (stats != nullptr) {
                ++stats->skipped_low_score;
            }
            continue;
        }

        rectangle = unclipRectangle(rectangle, config.unclip_ratio);
        if (std::min(rectangle.size.width, rectangle.size.height) < 5.0F) {
            if (stats != nullptr) {
                ++stats->skipped_small_boxes;
            }
            continue;
        }
        rectangle.points(points.data());
        points = orderedPoints(points);

        for (cv::Point2f& point : points) {
            point.x = std::clamp(std::round(point.x / static_cast<float>(probability_width) * image_size.width),
                                 0.0F,
                                 static_cast<float>(image_size.width));
            point.y = std::clamp(std::round(point.y / static_cast<float>(probability_height) * image_size.height),
                                 0.0F,
                                 static_cast<float>(image_size.height));
        }

        const double box_width = std::max(distance(points[0], points[1]), distance(points[2], points[3]));
        const double box_height = std::max(distance(points[0], points[3]), distance(points[1], points[2]));
        if (box_width < 1.0 || box_height < 1.0) {
            if (stats != nullptr) {
                ++stats->skipped_small_boxes;
            }
            continue;
        }

        boxes.push_back({points, bboxFromPoints(points, image_size), score});
        if (stats != nullptr) {
            ++stats->accepted_boxes;
        }
    }

    std::stable_sort(boxes.begin(), boxes.end(), [](const DetectionBox& lhs, const DetectionBox& rhs) {
        if (lhs.bbox.y0 != rhs.bbox.y0) {
            return lhs.bbox.y0 < rhs.bbox.y0;
        }
        return lhs.bbox.x0 < rhs.bbox.x0;
    });
    for (std::size_t index = 1; index < boxes.size(); ++index) {
        std::size_t current = index;
        while (current > 0 && std::fabs(boxes[current].bbox.y0 - boxes[current - 1].bbox.y0) < 10.0 &&
               boxes[current].bbox.x0 < boxes[current - 1].bbox.x0) {
            std::swap(boxes[current], boxes[current - 1]);
            --current;
        }
    }

    return boxes;
}

cv::Mat cropTextImage(const cv::Mat& image, const DetectionBox& box) {
    const std::array<cv::Point2f, 4> points = orderedPoints(box.points);
    const int crop_width =
        static_cast<int>(std::round(std::max(distance(points[0], points[1]), distance(points[2], points[3]))));
    const int crop_height =
        static_cast<int>(std::round(std::max(distance(points[0], points[3]), distance(points[1], points[2]))));
    if (crop_width <= 0 || crop_height <= 0) {
        return {};
    }

    const std::array<cv::Point2f, 4> destination{
        cv::Point2f{0.0F, 0.0F},
        cv::Point2f{static_cast<float>(crop_width - 1), 0.0F},
        cv::Point2f{static_cast<float>(crop_width - 1), static_cast<float>(crop_height - 1)},
        cv::Point2f{0.0F, static_cast<float>(crop_height - 1)},
    };

    const cv::Mat transform = cv::getPerspectiveTransform(points.data(), destination.data());
    cv::Mat crop;
    cv::warpPerspective(image, crop, transform, cv::Size(crop_width, crop_height), cv::INTER_CUBIC);

    if (crop.rows > 0 && crop.cols > 0 && static_cast<double>(crop.rows) / static_cast<double>(crop.cols) >= 1.5) {
        cv::rotate(crop, crop, cv::ROTATE_90_CLOCKWISE);
    }

    return crop;
}

int positiveShapeValue(const std::vector<int64_t>& shape, std::size_t index, int fallback) {
    if (index < shape.size() && shape[index] > 0 &&
        shape[index] < static_cast<int64_t>(std::numeric_limits<int>::max())) {
        return static_cast<int>(shape[index]);
    }
    return fallback;
}

RecognitionBatchInput makeRecognitionBatch(const std::vector<cv::Mat>& crops,
                                           const std::vector<std::size_t>& source_indices,
                                           const PaddleOcrOnnxConfig& config,
                                           const std::vector<int64_t>& model_shape) {
    const int target_height = positiveShapeValue(model_shape, 2, config.recognition_image_height);
    const int fixed_width = positiveShapeValue(model_shape, 3, 0);
    int target_width = fixed_width;
    if (target_width <= 0) {
        double max_width_ratio = static_cast<double>(config.recognition_base_width) / target_height;
        for (const std::size_t source_index : source_indices) {
            const cv::Mat& crop = crops[source_index];
            max_width_ratio =
                std::max(max_width_ratio, static_cast<double>(crop.cols) / static_cast<double>(std::max(1, crop.rows)));
        }
        target_width = static_cast<int>(std::ceil(target_height * max_width_ratio));
        target_width = roundUpToMultiple(target_width, config.recognition_width_multiple);
        target_width = std::clamp(target_width, config.recognition_base_width, config.recognition_max_width);
    }

    RecognitionBatchInput input;
    input.source_indices = source_indices;
    input.shape = {static_cast<int64_t>(source_indices.size()), 3, target_height, target_width};
    input.tensor.assign(source_indices.size() * static_cast<std::size_t>(3 * target_height * target_width), 0.0F);

    for (std::size_t batch_index = 0; batch_index < source_indices.size(); ++batch_index) {
        const cv::Mat& crop = crops[source_indices[batch_index]];
        const double width_ratio = static_cast<double>(crop.cols) / static_cast<double>(std::max(1, crop.rows));
        const int resized_width = std::clamp(static_cast<int>(std::ceil(target_height * width_ratio)), 1, target_width);

        cv::Mat resized;
        cv::resize(crop, resized, cv::Size(resized_width, target_height), 0.0, 0.0, cv::INTER_LINEAR);
        if (config.profile.convert_bgr_to_rgb) {
            cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
        }
        resized.convertTo(resized, CV_32FC3, config.profile.recognition_scale);

        for (int y = 0; y < target_height; ++y) {
            for (int x = 0; x < resized_width; ++x) {
                const cv::Vec3f pixel = resized.at<cv::Vec3f>(y, x);
                for (int channel = 0; channel < 3; ++channel) {
                    const std::size_t offset =
                        batch_index * static_cast<std::size_t>(3 * target_height * target_width) +
                        static_cast<std::size_t>(channel * target_height * target_width + y * target_width + x);
                    input.tensor[offset] = (pixel[channel] - config.profile.recognition_mean[channel]) /
                                           config.profile.recognition_std[channel];
                }
            }
        }
    }

    return input;
}

bool looksLikeProbabilityDistribution(const float* values, int class_count) {
    double sum = 0.0;
    for (int class_index = 0; class_index < class_count; ++class_index) {
        const float value = values[class_index];
        if (value < -1.0e-6F || value > 1.0F + 1.0e-6F) {
            return false;
        }
        sum += value;
    }
    return sum > 0.90 && sum < 1.10;
}

double classProbability(const float* values, int class_count, int index) {
    if (looksLikeProbabilityDistribution(values, class_count)) {
        return values[index];
    }

    const float max_value = *std::max_element(values, values + class_count);
    double sum = 0.0;
    for (int class_index = 0; class_index < class_count; ++class_index) {
        sum += std::exp(static_cast<double>(values[class_index] - max_value));
    }
    if (sum <= 0.0) {
        return 0.0;
    }
    return std::exp(static_cast<double>(values[index] - max_value)) / sum;
}

std::string labelForIndex(int index, const std::vector<std::string>& dictionary) {
    if (index <= 0) {
        return {};
    }
    const int dictionary_index = index - 1;
    if (dictionary_index >= 0 && dictionary_index < static_cast<int>(dictionary.size())) {
        return dictionary[static_cast<std::size_t>(dictionary_index)];
    }
    if (dictionary_index == static_cast<int>(dictionary.size())) {
        return " ";
    }
    return {};
}

RecognitionResult decodeRecognition(const Ort::Value& recognition_output,
                                    std::size_t batch_index,
                                    const std::vector<std::string>& dictionary,
                                    double threshold) {
    const float* logits = recognition_output.GetTensorData<float>();
    const auto shape = recognition_output.GetTensorTypeAndShapeInfo().GetShape();
    if (logits == nullptr || shape.size() < 2) {
        return {};
    }

    const int class_count = static_cast<int>(shape.back());
    const int time_steps = static_cast<int>(shape[shape.size() - 2]);
    const int64_t batch_count = shape.size() >= 3 ? shape[shape.size() - 3] : 1;
    if (class_count <= 0 || time_steps <= 0 || batch_count <= 0 ||
        batch_index >= static_cast<std::size_t>(batch_count)) {
        return {};
    }

    logits += static_cast<std::ptrdiff_t>(batch_index * static_cast<std::size_t>(time_steps * class_count));

    RecognitionResult result;
    int previous_index = -1;
    double confidence_sum = 0.0;
    int emitted_count = 0;

    for (int time_index = 0; time_index < time_steps; ++time_index) {
        const float* step_logits = logits + static_cast<std::ptrdiff_t>(time_index * class_count);
        const auto best = std::max_element(step_logits, step_logits + class_count);
        const int best_index = static_cast<int>(std::distance(step_logits, best));
        if (best_index == 0 || best_index == previous_index) {
            previous_index = best_index;
            continue;
        }

        const std::string label = labelForIndex(best_index, dictionary);
        if (!label.empty()) {
            result.text += label;
            confidence_sum += classProbability(step_logits, class_count, best_index);
            ++emitted_count;
        }
        previous_index = best_index;
    }

    if (emitted_count == 0) {
        return {};
    }

    result.confidence = confidence_sum / static_cast<double>(emitted_count);
    if (result.confidence < threshold) {
        return {};
    }

    return result;
}

cv::Mat cropAxisAligned(const cv::Mat& image, const document::BBox& bbox) {
    const int x0 = std::clamp(static_cast<int>(std::floor(bbox.x0)), 0, image.cols);
    const int y0 = std::clamp(static_cast<int>(std::floor(bbox.y0)), 0, image.rows);
    const int x1 = std::clamp(static_cast<int>(std::ceil(bbox.x1)), 0, image.cols);
    const int y1 = std::clamp(static_cast<int>(std::ceil(bbox.y1)), 0, image.rows);
    if (x1 <= x0 || y1 <= y0) {
        return {};
    }
    return image(cv::Rect(x0, y0, x1 - x0, y1 - y0)).clone();
}

bool recognizeCrops(Ort::Session& session,
                    const std::vector<std::string>& input_names,
                    const std::vector<std::string>& output_names,
                    const std::vector<int64_t>& model_shape,
                    const std::vector<std::string>& dictionary,
                    const PaddleOcrOnnxConfig& config,
                    const std::vector<cv::Mat>& crops,
                    std::vector<RecognitionResult>& recognitions,
                    RecognitionStats* stats,
                    bool debug) {
    recognitions.assign(crops.size(), {});
    if (crops.empty()) {
        return true;
    }

    std::vector<std::size_t> sorted_indices(crops.size());
    std::iota(sorted_indices.begin(), sorted_indices.end(), 0U);
    std::stable_sort(sorted_indices.begin(), sorted_indices.end(), [&](std::size_t lhs, std::size_t rhs) {
        const double lhs_ratio = static_cast<double>(crops[lhs].cols) / std::max(1, crops[lhs].rows);
        const double rhs_ratio = static_cast<double>(crops[rhs].cols) / std::max(1, crops[rhs].rows);
        return lhs_ratio < rhs_ratio;
    });

    const int fixed_batch = positiveShapeValue(model_shape, 0, 0);
    const std::size_t batch_capacity = fixed_batch > 0 ? static_cast<std::size_t>(fixed_batch)
                                                       : std::max<std::size_t>(1, config.recognition_batch_size);
    const std::vector<const char*> input_name_pointers = namePointers(input_names);
    const std::vector<const char*> output_name_pointers = namePointers(output_names);
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::size_t logged_texts = 0;

    for (std::size_t begin = 0; begin < sorted_indices.size(); begin += batch_capacity) {
        const std::size_t end = std::min(sorted_indices.size(), begin + batch_capacity);
        std::vector<std::size_t> batch_indices(sorted_indices.begin() + static_cast<std::ptrdiff_t>(begin),
                                               sorted_indices.begin() + static_cast<std::ptrdiff_t>(end));
        const std::size_t real_batch_size = batch_indices.size();
        if (fixed_batch > 0) {
            while (batch_indices.size() < batch_capacity) {
                batch_indices.push_back(batch_indices.back());
            }
        }

        RecognitionBatchInput batch = makeRecognitionBatch(crops, batch_indices, config, model_shape);
        Ort::Value tensor = Ort::Value::CreateTensor<float>(
            memory_info, batch.tensor.data(), batch.tensor.size(), batch.shape.data(), batch.shape.size());
        std::vector<Ort::Value> outputs = session.Run(Ort::RunOptions{nullptr},
                                                      input_name_pointers.data(),
                                                      &tensor,
                                                      1,
                                                      output_name_pointers.data(),
                                                      output_name_pointers.size());
        if (outputs.empty()) {
            return false;
        }
        if (stats != nullptr) {
            ++stats->inference_runs;
            if (stats->first_output_shape.empty()) {
                stats->first_output_shape = outputs.front().GetTensorTypeAndShapeInfo().GetShape();
            }
        }

        for (std::size_t batch_index = 0; batch_index < real_batch_size; ++batch_index) {
            RecognitionResult recognition =
                decodeRecognition(outputs.front(), batch_index, dictionary, config.recognition_threshold);
            recognitions[batch_indices[batch_index]] = recognition;
            if (recognition.text.empty()) {
                if (stats != nullptr) {
                    ++stats->empty_decodes;
                }
                continue;
            }
            if (stats != nullptr) {
                ++stats->decoded_texts;
            }
            if (debug && logged_texts < 5) {
                std::cerr << "[paddleocr] decoded text=\"" << recognition.text
                          << "\" confidence=" << recognition.confidence << '\n';
                ++logged_texts;
            }
        }
    }
    return true;
}

document::TextLine makeTextLine(const DetectionBox& box, const RecognitionResult& recognition) {
    document::TextSpan span;
    span.text = recognition.text;
    span.bbox = box.bbox;
    span.source = document::TextSource::Ocr;
    span.confidence = recognition.confidence;

    document::TextLine line;
    line.text = recognition.text;
    line.bbox = box.bbox;
    line.source = document::TextSource::Ocr;
    line.confidence = recognition.confidence;
    line.spans.push_back(std::move(span));
    return line;
}

bool detectBoxes(Ort::Session& session,
                 const std::vector<std::string>& input_names,
                 const std::vector<std::string>& output_names,
                 const cv::Mat& image,
                 const PaddleOcrOnnxConfig& config,
                 std::vector<DetectionBox>& boxes,
                 DetectionStats* stats,
                 bool debug) {
    const DetectionInput input = makeDetectionInput(image, config);
    if (debug) {
        std::cerr << "[paddleocr] profile=" << config.profile.name << " image=" << image.cols << 'x' << image.rows
                  << " det_input=[" << input.shape[0] << ',' << input.shape[1] << ',' << input.shape[2] << ','
                  << input.shape[3] << "]\n";
    }

    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory_info,
                                                        const_cast<float*>(input.tensor.data()),
                                                        input.tensor.size(),
                                                        input.shape.data(),
                                                        input.shape.size());
    const std::vector<const char*> input_name_pointers = namePointers(input_names);
    const std::vector<const char*> output_name_pointers = namePointers(output_names);
    std::vector<Ort::Value> outputs = session.Run(Ort::RunOptions{nullptr},
                                                  input_name_pointers.data(),
                                                  &tensor,
                                                  1,
                                                  output_name_pointers.data(),
                                                  output_name_pointers.size());
    if (outputs.empty()) {
        return false;
    }
    boxes = extractBoxes(outputs.front(), image.size(), config, stats);
    return true;
}

} // namespace

std::unique_ptr<PaddleOcrOnnxBackend::ModelBundle>
PaddleOcrOnnxBackend::loadModelBundle(const PaddleOcrOnnxConfig& config) {
    if (!fileExists(config.detection_model) || !fileExists(config.recognition_model) ||
        !fileExists(config.character_dict) ||
        (config.profile.name != "ppocrv4_mobile" && config.profile.name != "ppocrv5_mobile") ||
        config.detection_limit_side <= 0 || config.recognition_image_height <= 0 ||
        config.recognition_base_width <= 0 || config.recognition_max_width < config.recognition_base_width ||
        config.recognition_batch_size == 0 || config.detection_max_candidates == 0) {
        return nullptr;
    }

    try {
        auto model = std::make_unique<PaddleOcrOnnxBackend::ModelBundle>();
        model->dictionary = loadDictionary(config.character_dict);
        if (model->dictionary.empty()) {
            return nullptr;
        }

        model->detection_session = model->createSession(config.detection_model);
        model->recognition_session = model->createSession(config.recognition_model);

        model->detection_input_names = tensorNames(*model->detection_session, true);
        model->detection_output_names = tensorNames(*model->detection_session, false);
        model->recognition_input_names = tensorNames(*model->recognition_session, true);
        model->recognition_output_names = tensorNames(*model->recognition_session, false);
        model->recognition_input_shape = inputShape(*model->recognition_session, 0);

        if (model->detection_input_names.empty() || model->detection_output_names.empty() ||
            model->recognition_input_names.empty() || model->recognition_output_names.empty()) {
            return nullptr;
        }

        return model;
    } catch (const Ort::Exception&) {
        return nullptr;
    }
}

PaddleOcrOnnxBackend::PaddleOcrOnnxBackend() : PaddleOcrOnnxBackend(configFromEnvironment()) {}

PaddleOcrOnnxBackend::PaddleOcrOnnxBackend(PaddleOcrOnnxConfig config)
    : config_(std::move(config)), model_(loadModelBundle(config_)) {}

PaddleOcrOnnxBackend::~PaddleOcrOnnxBackend() = default;

PaddleOcrOnnxBackend::PaddleOcrOnnxBackend(PaddleOcrOnnxBackend&&) noexcept = default;

PaddleOcrOnnxBackend& PaddleOcrOnnxBackend::operator=(PaddleOcrOnnxBackend&&) noexcept = default;

bool PaddleOcrOnnxBackend::isAvailable() const { return model_ != nullptr; }

const PaddleOcrOnnxConfig& PaddleOcrOnnxBackend::config() const { return config_; }

bool PaddleOcrOnnxBackend::recognize(const OcrRequest& request, OcrResult& result) const {
    result = {};
    result.page_text = {};
    result.page_text.page_index = request.page.page_index;
    result.page_text.page_number = request.page.page_number;
    result.page_text.preferred_source = document::TextSource::Ocr;

    const bool debug = envFlag(kDebugEnv);
    if (model_ == nullptr || request.dpi <= 0 || !fileExists(request.page.output_path)) {
        if (debug) {
            std::cerr << "[paddleocr] unavailable input path=" << request.page.output_path.string()
                      << " dpi=" << request.dpi << " model=" << (model_ == nullptr ? "missing" : "loaded") << '\n';
        }
        return false;
    }

    const cv::Mat image = cv::imread(request.page.output_path.string(), cv::IMREAD_COLOR);
    if (image.empty()) {
        if (debug) {
            std::cerr << "[paddleocr] failed to read image path=" << request.page.output_path.string() << '\n';
        }
        return false;
    }

    try {
        DetectionStats detection_stats;
        std::vector<DetectionBox> boxes;
        if (!detectBoxes(*model_->detection_session,
                         model_->detection_input_names,
                         model_->detection_output_names,
                         image,
                         config_,
                         boxes,
                         &detection_stats,
                         debug)) {
            return false;
        }
        if (debug) {
            std::cerr << "[paddleocr] det_output_shape=" << shapeToString(detection_stats.output_shape)
                      << " prob_min=" << detection_stats.probability_min
                      << " prob_max=" << detection_stats.probability_max << " contours=" << detection_stats.contours
                      << " skipped_small_contours=" << detection_stats.skipped_small_contours
                      << " skipped_low_score=" << detection_stats.skipped_low_score
                      << " skipped_small_boxes=" << detection_stats.skipped_small_boxes << " boxes=" << boxes.size()
                      << '\n';
        }

        result.regions.reserve(boxes.size());
        std::vector<cv::Mat> crops;
        std::vector<std::size_t> crop_box_indices;
        RecognitionStats recognition_stats;
        for (std::size_t box_index = 0; box_index < boxes.size(); ++box_index) {
            const DetectionBox& box = boxes[box_index];
            result.regions.push_back({box.bbox, box.score, {}, 0.0});
            const cv::Mat crop = cropTextImage(image, box);
            if (crop.empty()) {
                ++recognition_stats.empty_crops;
                continue;
            }
            crops.push_back(crop);
            crop_box_indices.push_back(box_index);
            ++recognition_stats.crops;
        }

        std::vector<RecognitionResult> recognitions;
        if (!recognizeCrops(*model_->recognition_session,
                            model_->recognition_input_names,
                            model_->recognition_output_names,
                            model_->recognition_input_shape,
                            model_->dictionary,
                            config_,
                            crops,
                            recognitions,
                            &recognition_stats,
                            debug)) {
            return false;
        }
        for (std::size_t crop_index = 0; crop_index < crops.size(); ++crop_index) {
            const std::size_t box_index = crop_box_indices[crop_index];
            const RecognitionResult& recognition = recognitions[crop_index];
            result.regions[box_index].text = recognition.text;
            result.regions[box_index].recognition_confidence = recognition.confidence;
            if (!recognition.text.empty()) {
                result.page_text.lines.push_back(makeTextLine(boxes[box_index], recognition));
            }
        }

        if (debug) {
            std::cerr << "[paddleocr] rec_input_shape=" << shapeToString(model_->recognition_input_shape)
                      << " rec_output_shape=" << shapeToString(recognition_stats.first_output_shape)
                      << " crops=" << recognition_stats.crops << " empty_crops=" << recognition_stats.empty_crops
                      << " recognition_batches=" << recognition_stats.inference_runs
                      << " decoded_texts=" << recognition_stats.decoded_texts
                      << " empty_decodes=" << recognition_stats.empty_decodes << '\n';
        }
    } catch (const Ort::Exception& error) {
        if (debug) {
            std::cerr << "[paddleocr] ONNX Runtime exception during recognition: " << error.what() << '\n';
        }
        return false;
    } catch (const cv::Exception& error) {
        if (debug) {
            std::cerr << "[paddleocr] OpenCV exception during recognition: " << error.what() << '\n';
        }
        return false;
    }

    result.page_text.has_text = !result.page_text.lines.empty();
    result.page_text.preferred_source = result.page_text.has_text ? document::TextSource::Ocr
                                                                  : document::TextSource::Unknown;
    return true;
}

bool PaddleOcrOnnxBackend::detect(const OcrRequest& request, OcrDetectionResult& result) const {
    result = {};
    const bool debug = envFlag(kDebugEnv);
    if (model_ == nullptr || request.dpi <= 0 || !fileExists(request.page.output_path)) {
        return false;
    }
    const cv::Mat image = cv::imread(request.page.output_path.string(), cv::IMREAD_COLOR);
    if (image.empty()) {
        return false;
    }

    try {
        std::vector<DetectionBox> boxes;
        DetectionStats stats;
        if (!detectBoxes(*model_->detection_session,
                         model_->detection_input_names,
                         model_->detection_output_names,
                         image,
                         config_,
                         boxes,
                         &stats,
                         debug)) {
            return false;
        }
        result.regions.reserve(boxes.size());
        for (const DetectionBox& box : boxes) {
            result.regions.push_back({box.bbox, box.score, {}, 0.0});
        }
        if (debug) {
            std::cerr << "[paddleocr] detection_only boxes=" << result.regions.size() << '\n';
        }
        return true;
    } catch (const Ort::Exception& error) {
        if (debug) {
            std::cerr << "[paddleocr] detection ONNX Runtime exception: " << error.what() << '\n';
        }
    } catch (const cv::Exception& error) {
        if (debug) {
            std::cerr << "[paddleocr] detection OpenCV exception: " << error.what() << '\n';
        }
    }
    return false;
}

bool PaddleOcrOnnxBackend::recognizeRegions(const OcrRegionRequest& request, OcrRegionRecognitionResult& result) const {
    result = {};
    const bool debug = envFlag(kDebugEnv);
    if (model_ == nullptr || request.dpi <= 0 || !fileExists(request.page.output_path)) {
        return false;
    }
    const cv::Mat image = cv::imread(request.page.output_path.string(), cv::IMREAD_COLOR);
    if (image.empty()) {
        return false;
    }

    result.regions.reserve(request.regions.size());
    std::vector<cv::Mat> crops;
    std::vector<std::size_t> crop_region_indices;
    for (std::size_t region_index = 0; region_index < request.regions.size(); ++region_index) {
        result.regions.push_back({request.regions[region_index], 0.0, {}, 0.0});
        cv::Mat crop = cropAxisAligned(image, request.regions[region_index]);
        if (!crop.empty()) {
            crops.push_back(std::move(crop));
            crop_region_indices.push_back(region_index);
        }
    }

    try {
        RecognitionStats stats;
        stats.crops = crops.size();
        stats.empty_crops = request.regions.size() - crops.size();
        std::vector<RecognitionResult> recognitions;
        if (!recognizeCrops(*model_->recognition_session,
                            model_->recognition_input_names,
                            model_->recognition_output_names,
                            model_->recognition_input_shape,
                            model_->dictionary,
                            config_,
                            crops,
                            recognitions,
                            &stats,
                            debug)) {
            return false;
        }
        for (std::size_t crop_index = 0; crop_index < crops.size(); ++crop_index) {
            OcrRegion& region = result.regions[crop_region_indices[crop_index]];
            region.text = recognitions[crop_index].text;
            region.recognition_confidence = recognitions[crop_index].confidence;
        }
        if (debug) {
            std::cerr << "[paddleocr] ground_truth_regions=" << request.regions.size()
                      << " valid_crops=" << crops.size() << " recognition_batches=" << stats.inference_runs
                      << " decoded_texts=" << stats.decoded_texts << '\n';
        }
        return true;
    } catch (const Ort::Exception& error) {
        if (debug) {
            std::cerr << "[paddleocr] region recognition ONNX Runtime exception: " << error.what() << '\n';
        }
    } catch (const cv::Exception& error) {
        if (debug) {
            std::cerr << "[paddleocr] region recognition OpenCV exception: " << error.what() << '\n';
        }
    }
    return false;
}

} // namespace doc_parser::ocr
