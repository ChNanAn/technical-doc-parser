#include "ocr/paddle_ocr_onnx_backend.h"

#include "document/text_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <onnxruntime_cxx_api.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
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
    std::unique_ptr<Ort::Session> angle_classifier_session;
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
constexpr const char* kAngleClassifierModelEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_CLS_MODEL";
constexpr const char* kCharacterDictEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_DICT";

struct DetectionInput {
    std::vector<float> tensor;
    std::array<int64_t, 4> shape{};
    double width_scale = 1.0;
    double height_scale = 1.0;
};

struct RecognitionInput {
    std::vector<float> tensor;
    std::array<int64_t, 4> shape{};
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

std::filesystem::path envPath(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return {};
    }
    return std::filesystem::path(value);
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

PaddleOcrOnnxConfig configFromEnvironment() {
    PaddleOcrOnnxConfig config;
    config.detection_model = envPath(kDetectionModelEnv);
    config.recognition_model = envPath(kRecognitionModelEnv);
    config.angle_classifier_model = envPath(kAngleClassifierModelEnv);
    config.character_dict = envPath(kCharacterDictEnv);
    config.enable_angle_classifier = !config.angle_classifier_model.empty();
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
    cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
    resized.convertTo(resized, CV_32FC3, 1.0 / 255.0);

    constexpr std::array<float, 3> kMean{0.485F, 0.456F, 0.406F};
    constexpr std::array<float, 3> kStd{0.229F, 0.224F, 0.225F};

    DetectionInput input;
    input.shape = {1, 3, resized_height, resized_width};
    input.width_scale = static_cast<double>(resized_width) / static_cast<double>(original_width);
    input.height_scale = static_cast<double>(resized_height) / static_cast<double>(original_height);
    input.tensor.resize(static_cast<std::size_t>(3 * resized_width * resized_height));

    for (int y = 0; y < resized_height; ++y) {
        for (int x = 0; x < resized_width; ++x) {
            const cv::Vec3f pixel = resized.at<cv::Vec3f>(y, x);
            for (int channel = 0; channel < 3; ++channel) {
                const std::size_t offset =
                    static_cast<std::size_t>(channel * resized_width * resized_height + y * resized_width + x);
                input.tensor[offset] = (pixel[channel] - kMean[channel]) / kStd[channel];
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

std::vector<DetectionBox>
extractBoxes(const Ort::Value& detection_output, const cv::Size& image_size, const PaddleOcrOnnxConfig& config) {
    const float* probabilities = detection_output.GetTensorData<float>();
    const auto shape = detection_output.GetTensorTypeAndShapeInfo().GetShape();
    if (probabilities == nullptr || shape.size() < 2) {
        return {};
    }

    const int probability_width = static_cast<int>(shape.back());
    const int probability_height = static_cast<int>(shape[shape.size() - 2]);
    if (probability_width <= 0 || probability_height <= 0) {
        return {};
    }

    cv::Mat probability_map(probability_height, probability_width, CV_32FC1, const_cast<float*>(probabilities));
    cv::Mat bitmap;
    cv::threshold(probability_map, bitmap, config.detection_threshold, 255.0, cv::THRESH_BINARY);
    bitmap.convertTo(bitmap, CV_8UC1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    std::vector<DetectionBox> boxes;
    for (const auto& contour : contours) {
        if (contour.size() < 4 || std::fabs(cv::contourArea(contour)) < 4.0) {
            continue;
        }

        cv::Mat mask = cv::Mat::zeros(probability_height, probability_width, CV_8UC1);
        std::vector<std::vector<cv::Point>> single_contour{contour};
        cv::drawContours(mask, single_contour, 0, cv::Scalar(255), cv::FILLED);
        const double score = cv::mean(probability_map, mask)[0];
        if (score < config.box_threshold) {
            continue;
        }

        cv::RotatedRect rectangle = cv::minAreaRect(contour);
        rectangle.size.width = static_cast<float>(rectangle.size.width * config.unclip_ratio);
        rectangle.size.height = static_cast<float>(rectangle.size.height * config.unclip_ratio);

        std::array<cv::Point2f, 4> points{};
        rectangle.points(points.data());
        points = orderedPoints(points);

        for (cv::Point2f& point : points) {
            point.x = std::clamp(point.x * static_cast<float>(image_size.width) / static_cast<float>(probability_width),
                                 0.0F,
                                 static_cast<float>(image_size.width));
            point.y =
                std::clamp(point.y * static_cast<float>(image_size.height) / static_cast<float>(probability_height),
                           0.0F,
                           static_cast<float>(image_size.height));
        }

        const double box_width = std::max(distance(points[0], points[1]), distance(points[2], points[3]));
        const double box_height = std::max(distance(points[0], points[3]), distance(points[1], points[2]));
        if (box_width < 3.0 || box_height < 3.0) {
            continue;
        }

        boxes.push_back({points, bboxFromPoints(points, image_size), score});
    }

    std::sort(boxes.begin(), boxes.end(), [](const DetectionBox& lhs, const DetectionBox& rhs) {
        if (std::fabs(lhs.bbox.y0 - rhs.bbox.y0) > 10.0) {
            return lhs.bbox.y0 < rhs.bbox.y0;
        }
        return lhs.bbox.x0 < rhs.bbox.x0;
    });

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

RecognitionInput makeRecognitionInput(const cv::Mat& bgr_crop,
                                      const PaddleOcrOnnxConfig& config,
                                      const std::vector<int64_t>& model_shape) {
    const int target_height = positiveShapeValue(model_shape, 2, config.recognition_image_height);
    const int target_width = positiveShapeValue(model_shape, 3, config.recognition_image_width);
    const double width_ratio = static_cast<double>(bgr_crop.cols) / static_cast<double>(std::max(1, bgr_crop.rows));
    const int resized_width = std::clamp(static_cast<int>(std::ceil(target_height * width_ratio)), 1, target_width);

    cv::Mat resized;
    cv::resize(bgr_crop, resized, cv::Size(resized_width, target_height), 0.0, 0.0, cv::INTER_LINEAR);
    cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
    resized.convertTo(resized, CV_32FC3, 1.0 / 255.0);

    cv::Mat padded = cv::Mat::zeros(target_height, target_width, CV_32FC3);
    resized.copyTo(padded(cv::Rect(0, 0, resized_width, target_height)));

    RecognitionInput input;
    input.shape = {1, 3, target_height, target_width};
    input.tensor.resize(static_cast<std::size_t>(3 * target_height * target_width));

    for (int y = 0; y < target_height; ++y) {
        for (int x = 0; x < target_width; ++x) {
            const cv::Vec3f pixel = padded.at<cv::Vec3f>(y, x);
            for (int channel = 0; channel < 3; ++channel) {
                const std::size_t offset =
                    static_cast<std::size_t>(channel * target_height * target_width + y * target_width + x);
                input.tensor[offset] = (pixel[channel] - 0.5F) / 0.5F;
            }
        }
    }

    return input;
}

double softmaxProbability(const float* logits, int class_count, int index) {
    const float max_value = *std::max_element(logits, logits + class_count);
    double sum = 0.0;
    for (int class_index = 0; class_index < class_count; ++class_index) {
        sum += std::exp(static_cast<double>(logits[class_index] - max_value));
    }
    if (sum <= 0.0) {
        return 0.0;
    }
    return std::exp(static_cast<double>(logits[index] - max_value)) / sum;
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

RecognitionResult
decodeRecognition(const Ort::Value& recognition_output, const std::vector<std::string>& dictionary, double threshold) {
    const float* logits = recognition_output.GetTensorData<float>();
    const auto shape = recognition_output.GetTensorTypeAndShapeInfo().GetShape();
    if (logits == nullptr || shape.size() < 2) {
        return {};
    }

    const int class_count = static_cast<int>(shape.back());
    const int time_steps = static_cast<int>(shape[shape.size() - 2]);
    if (class_count <= 0 || time_steps <= 0) {
        return {};
    }

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
            confidence_sum += softmaxProbability(step_logits, class_count, best_index);
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

} // namespace

std::unique_ptr<PaddleOcrOnnxBackend::ModelBundle>
PaddleOcrOnnxBackend::loadModelBundle(const PaddleOcrOnnxConfig& config) {
    if (!fileExists(config.detection_model) || !fileExists(config.recognition_model) ||
        !fileExists(config.character_dict)) {
        return nullptr;
    }

    if (config.enable_angle_classifier && !fileExists(config.angle_classifier_model)) {
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
        if (config.enable_angle_classifier) {
            model->angle_classifier_session = model->createSession(config.angle_classifier_model);
        }

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

bool PaddleOcrOnnxBackend::recognize(const OcrRequest& request, OcrResult& result) const {
    result.page_text = {};
    result.page_text.page_index = request.page.page_index;
    result.page_text.page_number = request.page.page_number;
    result.page_text.preferred_source = document::TextSource::Unknown;

    if (model_ == nullptr || request.dpi <= 0 || !fileExists(request.page.output_path)) {
        return false;
    }

    const cv::Mat image = cv::imread(request.page.output_path.string(), cv::IMREAD_COLOR);
    if (image.empty()) {
        return false;
    }

    try {
        const DetectionInput detection_input = makeDetectionInput(image, config_);
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value detection_tensor = Ort::Value::CreateTensor<float>(memory_info,
                                                                      const_cast<float*>(detection_input.tensor.data()),
                                                                      detection_input.tensor.size(),
                                                                      detection_input.shape.data(),
                                                                      detection_input.shape.size());
        const std::vector<const char*> detection_input_names = namePointers(model_->detection_input_names);
        const std::vector<const char*> detection_output_names = namePointers(model_->detection_output_names);
        std::vector<Ort::Value> detection_outputs = model_->detection_session->Run(Ort::RunOptions{nullptr},
                                                                                   detection_input_names.data(),
                                                                                   &detection_tensor,
                                                                                   1,
                                                                                   detection_output_names.data(),
                                                                                   detection_output_names.size());
        if (detection_outputs.empty()) {
            return false;
        }

        const std::vector<DetectionBox> boxes = extractBoxes(detection_outputs.front(), image.size(), config_);
        const std::vector<const char*> recognition_input_names = namePointers(model_->recognition_input_names);
        const std::vector<const char*> recognition_output_names = namePointers(model_->recognition_output_names);

        for (const DetectionBox& box : boxes) {
            const cv::Mat crop = cropTextImage(image, box);
            if (crop.empty()) {
                continue;
            }

            const RecognitionInput recognition_input =
                makeRecognitionInput(crop, config_, model_->recognition_input_shape);
            Ort::Value recognition_tensor =
                Ort::Value::CreateTensor<float>(memory_info,
                                                const_cast<float*>(recognition_input.tensor.data()),
                                                recognition_input.tensor.size(),
                                                recognition_input.shape.data(),
                                                recognition_input.shape.size());
            std::vector<Ort::Value> recognition_outputs =
                model_->recognition_session->Run(Ort::RunOptions{nullptr},
                                                 recognition_input_names.data(),
                                                 &recognition_tensor,
                                                 1,
                                                 recognition_output_names.data(),
                                                 recognition_output_names.size());
            if (recognition_outputs.empty()) {
                continue;
            }

            const RecognitionResult recognition =
                decodeRecognition(recognition_outputs.front(), model_->dictionary, config_.recognition_threshold);
            if (!recognition.text.empty()) {
                result.page_text.lines.push_back(makeTextLine(box, recognition));
            }
        }
    } catch (const Ort::Exception&) {
        return false;
    } catch (const cv::Exception&) {
        return false;
    }

    result.page_text.has_text = !result.page_text.lines.empty();
    result.page_text.preferred_source = result.page_text.has_text ? document::TextSource::Ocr
                                                                  : document::TextSource::Unknown;
    return true;
}

} // namespace doc_parser::ocr
