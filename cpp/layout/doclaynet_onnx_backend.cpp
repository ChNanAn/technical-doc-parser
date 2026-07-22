#include "layout/layout_backend.h"
#include "layout/layout_postprocessing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace doc_parser::layout {

struct DocLayNetOnnxBackend::ModelBundle {
    ModelBundle() : environment(ORT_LOGGING_LEVEL_WARNING, "document-intelligence-engine-doclaynet") {
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
    }

    Ort::Env environment;
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> session;
    std::string input_name;
    std::vector<std::string> output_names;
    std::size_t boxes_output_index = 0;
    std::size_t logits_output_index = 0;
};

namespace {

constexpr const char* kModelEnv = "DOCUMENT_INTELLIGENCE_ENGINE_DOCLAYNET_MODEL";
constexpr const char* kConfidenceEnv = "DOCUMENT_INTELLIGENCE_ENGINE_DOCLAYNET_CONFIDENCE";
constexpr const char* kDebugEnv = "DOCUMENT_INTELLIGENCE_ENGINE_LAYOUT_DEBUG";

#ifndef DOC_PARSER_DOCLAYNET_MODEL_PATH
#define DOC_PARSER_DOCLAYNET_MODEL_PATH "models/layout/doclaynet/model.onnx"
#endif

constexpr std::array<const char*, 11> kDocLayNetLabels{
    "Caption",
    "Footnote",
    "Formula",
    "List-item",
    "Page-footer",
    "Page-header",
    "Picture",
    "Section-header",
    "Table",
    "Text",
    "Title",
};

struct ModelInput {
    std::vector<float> tensor;
    std::array<int64_t, 4> shape{};
};

bool envFlag(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return false;
    }
    const std::string flag(value);
    return flag != "0" && flag != "false" && flag != "FALSE" && flag != "off" && flag != "OFF";
}

double envProbability(const char* name, double fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return fallback;
    }
    try {
        const double parsed = std::stod(value);
        return parsed > 0.0 && parsed < 1.0 ? parsed : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}

bool fileExists(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_regular_file(path, error);
}

DocLayNetOnnxConfig configFromEnvironment() {
    DocLayNetOnnxConfig config;
    const char* model = std::getenv(kModelEnv);
    config.model_path = model == nullptr || std::string(model).empty() ? DOC_PARSER_DOCLAYNET_MODEL_PATH : model;
    config.confidence_threshold = envProbability(kConfidenceEnv, config.confidence_threshold);
    return config;
}

std::vector<std::string> tensorNames(const Ort::Session& session, bool inputs) {
    Ort::AllocatorWithDefaultOptions allocator;
    const std::size_t count = inputs ? session.GetInputCount() : session.GetOutputCount();
    std::vector<std::string> names;
    names.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        auto name = inputs ? session.GetInputNameAllocated(index, allocator)
                           : session.GetOutputNameAllocated(index, allocator);
        names.emplace_back(name.get());
    }
    return names;
}

std::vector<const char*> namePointers(const std::vector<std::string>& names) {
    std::vector<const char*> pointers;
    pointers.reserve(names.size());
    for (const std::string& name : names) {
        pointers.push_back(name.c_str());
    }
    return pointers;
}

std::string blockId(int page_number, std::size_t index) {
    std::ostringstream stream;
    stream << "page_" << page_number << "_block_" << index + 1;
    return stream.str();
}

double sigmoid(float value) {
    if (value >= 0.0F) {
        return 1.0 / (1.0 + std::exp(-static_cast<double>(value)));
    }
    const double exponential = std::exp(static_cast<double>(value));
    return exponential / (1.0 + exponential);
}

double bboxArea(const document::BBox& bbox) {
    return std::max(0.0, bbox.x1 - bbox.x0) * std::max(0.0, bbox.y1 - bbox.y0);
}

ModelInput makeInput(const cv::Mat& bgr_image, const DocLayNetOnnxConfig& config) {
    cv::Mat resized;
    cv::resize(bgr_image, resized, cv::Size(config.input_width, config.input_height), 0.0, 0.0, cv::INTER_LINEAR);
    cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
    resized.convertTo(resized, CV_32FC3, 1.0 / 255.0);

    constexpr std::array<float, 3> kMean{0.485F, 0.456F, 0.406F};
    constexpr std::array<float, 3> kStd{0.229F, 0.224F, 0.225F};
    ModelInput input;
    input.shape = {1, 3, config.input_height, config.input_width};
    input.tensor.resize(3U * static_cast<std::size_t>(config.input_height) *
                        static_cast<std::size_t>(config.input_width));
    for (int y = 0; y < config.input_height; ++y) {
        for (int x = 0; x < config.input_width; ++x) {
            const cv::Vec3f pixel = resized.at<cv::Vec3f>(y, x);
            for (int channel = 0; channel < 3; ++channel) {
                const std::size_t offset =
                    static_cast<std::size_t>(channel) * config.input_height * config.input_width +
                    static_cast<std::size_t>(y) * config.input_width + x;
                input.tensor[offset] = (pixel[channel] - kMean[channel]) / kStd[channel];
            }
        }
    }
    return input;
}

bool decode(const Ort::Value& boxes_value,
            const Ort::Value& logits_value,
            const cv::Size& image_size,
            double confidence_threshold,
            std::vector<document::LayoutBlock>& blocks) {
    const std::vector<int64_t> boxes_shape = boxes_value.GetTensorTypeAndShapeInfo().GetShape();
    const std::vector<int64_t> logits_shape = logits_value.GetTensorTypeAndShapeInfo().GetShape();
    if (boxes_shape.size() != 3 || logits_shape.size() != 3 || boxes_shape[0] != 1 || logits_shape[0] != 1 ||
        boxes_shape[1] <= 0 || boxes_shape[2] != 4 || logits_shape[1] != boxes_shape[1] ||
        logits_shape[2] != static_cast<int64_t>(kDocLayNetLabels.size())) {
        return false;
    }

    const float* boxes_data = boxes_value.GetTensorData<float>();
    const float* logits_data = logits_value.GetTensorData<float>();
    if (boxes_data == nullptr || logits_data == nullptr) {
        return false;
    }

    for (int64_t query = 0; query < boxes_shape[1]; ++query) {
        const float* logits = logits_data + query * static_cast<int64_t>(kDocLayNetLabels.size());
        const auto best = std::max_element(logits, logits + kDocLayNetLabels.size());
        const std::size_t label_index = static_cast<std::size_t>(std::distance(logits, best));
        const double confidence = sigmoid(*best);
        if (confidence < confidence_threshold) {
            continue;
        }

        const float* box = boxes_data + query * 4;
        const double center_x = box[0] * image_size.width;
        const double center_y = box[1] * image_size.height;
        const double width = box[2] * image_size.width;
        const double height = box[3] * image_size.height;
        document::BBox bbox{
            std::clamp(center_x - width * 0.5, 0.0, static_cast<double>(image_size.width)),
            std::clamp(center_y - height * 0.5, 0.0, static_cast<double>(image_size.height)),
            std::clamp(center_x + width * 0.5, 0.0, static_cast<double>(image_size.width)),
            std::clamp(center_y + height * 0.5, 0.0, static_cast<double>(image_size.height)),
        };
        if (bboxArea(bbox) <= 0.0) {
            continue;
        }

        document::LayoutBlock block;
        block.type = mapDocLayNetLabel(kDocLayNetLabels[label_index]);
        block.source_label = kDocLayNetLabels[label_index];
        block.bbox = bbox;
        block.confidence = confidence;
        blocks.push_back(std::move(block));
    }
    return true;
}

} // namespace

std::unique_ptr<DocLayNetOnnxBackend::ModelBundle> DocLayNetOnnxBackend::loadModel(const DocLayNetOnnxConfig& config) {
    if (!fileExists(config.model_path) || config.input_width <= 0 || config.input_height <= 0 ||
        config.confidence_threshold <= 0.0 || config.confidence_threshold >= 1.0) {
        return nullptr;
    }

    try {
        auto model = std::make_unique<ModelBundle>();
        model->session = std::make_unique<Ort::Session>(
            model->environment, config.model_path.string().c_str(), model->session_options);
        const std::vector<std::string> input_names = tensorNames(*model->session, true);
        model->output_names = tensorNames(*model->session, false);
        if (input_names.size() != 1 || model->output_names.size() < 2) {
            return nullptr;
        }
        const std::vector<int64_t> input_shape =
            model->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (input_shape.size() != 4 || input_shape[0] != 1 || input_shape[1] != 3 ||
            input_shape[2] != config.input_height || input_shape[3] != config.input_width) {
            return nullptr;
        }
        model->input_name = input_names.front();
        const auto boxes = std::find(model->output_names.begin(), model->output_names.end(), "pred_boxes");
        const auto logits = std::find(model->output_names.begin(), model->output_names.end(), "pred_logits");
        if (boxes == model->output_names.end() || logits == model->output_names.end()) {
            return nullptr;
        }
        model->boxes_output_index = static_cast<std::size_t>(std::distance(model->output_names.begin(), boxes));
        model->logits_output_index = static_cast<std::size_t>(std::distance(model->output_names.begin(), logits));
        return model;
    } catch (const Ort::Exception& error) {
        if (envFlag(kDebugEnv)) {
            std::cerr << "[doclaynet] failed to load model: " << error.what() << '\n';
        }
        return nullptr;
    }
}

DocLayNetOnnxBackend::DocLayNetOnnxBackend() : DocLayNetOnnxBackend(configFromEnvironment()) {}

DocLayNetOnnxBackend::DocLayNetOnnxBackend(DocLayNetOnnxConfig config)
    : config_(std::move(config)), model_(loadModel(config_)) {}

DocLayNetOnnxBackend::~DocLayNetOnnxBackend() = default;

DocLayNetOnnxBackend::DocLayNetOnnxBackend(DocLayNetOnnxBackend&&) noexcept = default;

DocLayNetOnnxBackend& DocLayNetOnnxBackend::operator=(DocLayNetOnnxBackend&&) noexcept = default;

bool DocLayNetOnnxBackend::isAvailable() const { return model_ != nullptr; }

const DocLayNetOnnxConfig& DocLayNetOnnxBackend::config() const { return config_; }

bool DocLayNetOnnxBackend::analyze(const LayoutRequest& request, LayoutResult& result) const {
    result = {};
    result.layout.page_index = request.page.page_index;
    result.layout.page_number = request.page.page_number;
    const bool debug = envFlag(kDebugEnv);
    if (model_ == nullptr || !fileExists(request.page.output_path)) {
        return false;
    }

    const cv::Mat image = cv::imread(request.page.output_path.string(), cv::IMREAD_COLOR);
    if (image.empty()) {
        return false;
    }

    try {
        ModelInput input = makeInput(image, config_);
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value tensor = Ort::Value::CreateTensor<float>(
            memory_info, input.tensor.data(), input.tensor.size(), input.shape.data(), input.shape.size());
        const char* input_name = model_->input_name.c_str();
        const std::vector<const char*> output_names = namePointers(model_->output_names);
        std::vector<Ort::Value> outputs = model_->session->Run(
            Ort::RunOptions{nullptr}, &input_name, &tensor, 1, output_names.data(), output_names.size());
        if (outputs.size() != model_->output_names.size() || !decode(outputs[model_->boxes_output_index],
                                                                     outputs[model_->logits_output_index],
                                                                     image.size(),
                                                                     config_.confidence_threshold,
                                                                     result.layout.blocks)) {
            return false;
        }

        std::stable_sort(
            result.layout.blocks.begin(), result.layout.blocks.end(), [](const auto& lhs, const auto& rhs) {
                if (lhs.bbox.y0 != rhs.bbox.y0) {
                    return lhs.bbox.y0 < rhs.bbox.y0;
                }
                return lhs.bbox.x0 < rhs.bbox.x0;
            });
        for (std::size_t index = 0; index < result.layout.blocks.size(); ++index) {
            result.layout.blocks[index].id = blockId(request.page.page_number, index);
        }
        detail::assignTextLines(request.text, result.layout.blocks);
        detail::associateCaptions(request.page, result.layout.blocks);
        if (debug) {
            std::size_t assigned_lines = 0;
            std::size_t captions = 0;
            std::size_t linked_captions = 0;
            for (const document::LayoutBlock& block : result.layout.blocks) {
                assigned_lines += block.text_line_indices.size();
                if (block.source_label == "Caption") {
                    ++captions;
                    linked_captions += block.related_block_id.empty() ? 0U : 1U;
                }
            }
            std::cerr << "[doclaynet] image=" << image.cols << 'x' << image.rows << " input=" << config_.input_width
                      << 'x' << config_.input_height << " threshold=" << config_.confidence_threshold
                      << " blocks=" << result.layout.blocks.size() << " assigned_text_lines=" << assigned_lines
                      << " captions=" << captions << " linked_captions=" << linked_captions << '\n';
        }
        return true;
    } catch (const Ort::Exception& error) {
        if (debug) {
            std::cerr << "[doclaynet] ONNX Runtime exception: " << error.what() << '\n';
        }
    } catch (const cv::Exception& error) {
        if (debug) {
            std::cerr << "[doclaynet] OpenCV exception: " << error.what() << '\n';
        }
    }
    return false;
}

} // namespace doc_parser::layout
