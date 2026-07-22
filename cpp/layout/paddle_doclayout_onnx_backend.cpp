#include "layout/layout_backend.h"
#include "layout/layout_postprocessing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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

namespace doc_parser::layout {

struct PaddleDocLayoutOnnxBackend::ModelBundle {
    ModelBundle() : environment(ORT_LOGGING_LEVEL_WARNING, "document-intelligence-engine-paddle-doclayout") {
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
    }

    Ort::Env environment;
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> session;
    std::string image_input_name;
    std::string image_shape_input_name;
    std::string scale_factor_input_name;
    std::string boxes_output_name;
    std::string box_count_output_name;
};

namespace {

constexpr const char* kModelEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLE_LAYOUT_MODEL";
constexpr const char* kConfidenceEnv = "DOCUMENT_INTELLIGENCE_ENGINE_PADDLE_LAYOUT_CONFIDENCE";
constexpr const char* kDebugEnv = "DOCUMENT_INTELLIGENCE_ENGINE_LAYOUT_DEBUG";

#ifndef DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH
#define DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH "models/layout/paddle/pp-doclayout-v3.onnx"
#endif

constexpr std::array<const char*, 25> kPaddleLabels{
    "abstract",        "algorithm",         "aside_text", "chart",          "content",  "display_formula",
    "doc_title",       "figure_title",      "footer",     "footer_image",   "footnote", "formula_number",
    "header",          "header_image",      "image",      "inline_formula", "number",   "paragraph_title",
    "reference",       "reference_content", "seal",       "table",          "text",     "vertical_text",
    "vision_footnote",
};

struct ModelInput {
    std::vector<float> image;
    std::array<int64_t, 4> image_shape{};
    std::array<float, 2> resized_shape{};
    std::array<float, 2> scale_factor{};
    std::array<int64_t, 2> pair_shape{1, 2};
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

PaddleDocLayoutOnnxConfig configFromEnvironment() {
    PaddleDocLayoutOnnxConfig config;
    const char* model = std::getenv(kModelEnv);
    config.model_path = model == nullptr || std::string(model).empty() ? DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH : model;
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

bool containsName(const std::vector<std::string>& names, const std::string& expected) {
    return std::find(names.begin(), names.end(), expected) != names.end();
}

std::size_t inputIndex(const Ort::Session& session, const std::string& expected) {
    const std::vector<std::string> names = tensorNames(session, true);
    const auto found = std::find(names.begin(), names.end(), expected);
    return found == names.end() ? names.size() : static_cast<std::size_t>(std::distance(names.begin(), found));
}

bool matchesShape(const Ort::Session& session,
                  const std::string& input_name,
                  const std::vector<int64_t>& expected_shape) {
    const std::size_t index = inputIndex(session, input_name);
    if (index >= session.GetInputCount()) {
        return false;
    }
    const std::vector<int64_t> shape = session.GetInputTypeInfo(index).GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != expected_shape.size()) {
        return false;
    }
    for (std::size_t dimension = 0; dimension < shape.size(); ++dimension) {
        if (shape[dimension] > 0 && shape[dimension] != expected_shape[dimension]) {
            return false;
        }
    }
    return true;
}

std::string blockId(int page_number, std::size_t index) {
    std::ostringstream stream;
    stream << "page_" << page_number << "_block_" << index + 1;
    return stream.str();
}

double bboxArea(const document::BBox& bbox) {
    return std::max(0.0, bbox.x1 - bbox.x0) * std::max(0.0, bbox.y1 - bbox.y0);
}

double overlapOverSmaller(const document::BBox& lhs, const document::BBox& rhs) {
    const double width = std::max(0.0, std::min(lhs.x1, rhs.x1) - std::max(lhs.x0, rhs.x0));
    const double height = std::max(0.0, std::min(lhs.y1, rhs.y1) - std::max(lhs.y0, rhs.y0));
    const double denominator = std::min(bboxArea(lhs), bboxArea(rhs));
    return denominator <= 0.0 ? 0.0 : width * height / denominator;
}

bool isVisualLabel(const std::string& label) {
    return label == "image" || label == "table" || label == "seal" || label == "chart";
}

bool preserveVisualOverlap(const std::string& lhs, const std::string& rhs) {
    if (lhs == rhs || (!isVisualLabel(lhs) && !isVisualLabel(rhs))) {
        return false;
    }
    if (lhs != "table" && rhs != "table") {
        return true;
    }
    return isVisualLabel(lhs) && isVisualLabel(rhs);
}

void filterOverlappingBlocks(std::vector<document::LayoutBlock>& blocks) {
    std::vector<bool> dropped(blocks.size(), false);
    for (std::size_t lhs_index = 0; lhs_index < blocks.size(); ++lhs_index) {
        const double width = blocks[lhs_index].bbox.x1 - blocks[lhs_index].bbox.x0;
        const double height = blocks[lhs_index].bbox.y1 - blocks[lhs_index].bbox.y0;
        if (width < 6.0 || height < 6.0) {
            dropped[lhs_index] = true;
        }
        for (std::size_t rhs_index = lhs_index + 1; rhs_index < blocks.size(); ++rhs_index) {
            if (dropped[lhs_index] || dropped[rhs_index]) {
                continue;
            }
            const double overlap = overlapOverSmaller(blocks[lhs_index].bbox, blocks[rhs_index].bbox);
            if (blocks[lhs_index].source_label == "inline_formula" || blocks[rhs_index].source_label == "inline_"
                                                                                                        "formula") {
                if (overlap > 0.5) {
                    dropped[lhs_index] = blocks[lhs_index].source_label == "inline_formula";
                    dropped[rhs_index] = blocks[rhs_index].source_label == "inline_formula";
                }
                continue;
            }
            if (overlap <= 0.7 ||
                preserveVisualOverlap(blocks[lhs_index].source_label, blocks[rhs_index].source_label)) {
                continue;
            }
            if (bboxArea(blocks[lhs_index].bbox) >= bboxArea(blocks[rhs_index].bbox)) {
                dropped[rhs_index] = true;
            } else {
                dropped[lhs_index] = true;
            }
        }
    }

    std::vector<document::LayoutBlock> filtered;
    filtered.reserve(blocks.size());
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (!dropped[index]) {
            filtered.push_back(std::move(blocks[index]));
        }
    }
    blocks = std::move(filtered);
}

void filterPageSizedImages(const cv::Size& image_size, std::vector<document::LayoutBlock>& blocks) {
    if (blocks.size() <= 1) {
        return;
    }
    const double page_area = static_cast<double>(image_size.width) * image_size.height;
    const double maximum_ratio = image_size.width > image_size.height ? 0.82 : 0.93;
    std::vector<document::LayoutBlock> filtered;
    filtered.reserve(blocks.size());
    std::copy_if(blocks.begin(), blocks.end(), std::back_inserter(filtered), [&](const auto& block) {
        return block.source_label != "image" || bboxArea(block.bbox) <= maximum_ratio * page_area;
    });
    if (!filtered.empty()) {
        blocks = std::move(filtered);
    }
}

ModelInput makeInput(const cv::Mat& bgr_image, const PaddleDocLayoutOnnxConfig& config) {
    cv::Mat resized;
    cv::resize(bgr_image, resized, cv::Size(config.input_width, config.input_height), 0.0, 0.0, cv::INTER_CUBIC);
    cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
    resized.convertTo(resized, CV_32FC3, 1.0 / 255.0);

    ModelInput input;
    input.image_shape = {1, 3, config.input_height, config.input_width};
    input.resized_shape = {static_cast<float>(config.input_height), static_cast<float>(config.input_width)};
    input.scale_factor = {
        static_cast<float>(config.input_height) / bgr_image.rows,
        static_cast<float>(config.input_width) / bgr_image.cols,
    };
    input.image.resize(3U * static_cast<std::size_t>(config.input_height) *
                       static_cast<std::size_t>(config.input_width));
    for (int y = 0; y < config.input_height; ++y) {
        for (int x = 0; x < config.input_width; ++x) {
            const cv::Vec3f pixel = resized.at<cv::Vec3f>(y, x);
            for (int channel = 0; channel < 3; ++channel) {
                const std::size_t offset =
                    static_cast<std::size_t>(channel) * config.input_height * config.input_width +
                    static_cast<std::size_t>(y) * config.input_width + x;
                input.image[offset] = pixel[channel];
            }
        }
    }
    return input;
}

bool decode(const Ort::Value& boxes_value,
            const Ort::Value& counts_value,
            const cv::Size& image_size,
            double threshold,
            std::vector<document::LayoutBlock>& blocks,
            std::size_t& raw_count) {
    const std::vector<int64_t> boxes_shape = boxes_value.GetTensorTypeAndShapeInfo().GetShape();
    const std::vector<int64_t> counts_shape = counts_value.GetTensorTypeAndShapeInfo().GetShape();
    if (boxes_shape.size() != 2 || boxes_shape[1] != 7 || counts_shape.size() != 1 || counts_shape[0] < 1) {
        return false;
    }
    const float* boxes_data = boxes_value.GetTensorData<float>();
    const int32_t* counts_data = counts_value.GetTensorData<int32_t>();
    if (boxes_data == nullptr || counts_data == nullptr || counts_data[0] < 0) {
        return false;
    }

    raw_count = std::min<std::size_t>(static_cast<std::size_t>(counts_data[0]),
                                      static_cast<std::size_t>(std::max<int64_t>(0, boxes_shape[0])));
    for (std::size_t index = 0; index < raw_count; ++index) {
        const float* row = boxes_data + index * 7;
        const int label_index = static_cast<int>(std::round(row[0]));
        if (label_index < 0 || label_index >= static_cast<int>(kPaddleLabels.size()) || row[1] <= threshold) {
            continue;
        }
        const std::string label = kPaddleLabels[static_cast<std::size_t>(label_index)];
        if (label == "reference") {
            continue;
        }

        document::BBox bbox{
            std::clamp(std::round(static_cast<double>(row[2])), 0.0, static_cast<double>(image_size.width)),
            std::clamp(std::round(static_cast<double>(row[3])), 0.0, static_cast<double>(image_size.height)),
            std::clamp(std::round(static_cast<double>(row[4])), 0.0, static_cast<double>(image_size.width)),
            std::clamp(std::round(static_cast<double>(row[5])), 0.0, static_cast<double>(image_size.height)),
        };
        if (bboxArea(bbox) <= 0.0) {
            continue;
        }

        document::LayoutBlock block;
        block.type = mapPaddleDocLayoutLabel(label);
        block.source_label = label;
        block.bbox = bbox;
        block.confidence = row[1];
        block.reading_order_hint = static_cast<int>(std::round(row[6]));
        blocks.push_back(std::move(block));
    }

    std::stable_sort(blocks.begin(), blocks.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.reading_order_hint < rhs.reading_order_hint;
    });
    filterPageSizedImages(image_size, blocks);
    filterOverlappingBlocks(blocks);
    return true;
}

} // namespace

std::unique_ptr<PaddleDocLayoutOnnxBackend::ModelBundle>
PaddleDocLayoutOnnxBackend::loadModel(const PaddleDocLayoutOnnxConfig& config) {
    if (!fileExists(config.model_path) || config.input_width <= 0 || config.input_height <= 0 ||
        config.confidence_threshold <= 0.0 || config.confidence_threshold >= 1.0) {
        return nullptr;
    }

    try {
        auto model = std::make_unique<ModelBundle>();
        model->session = std::make_unique<Ort::Session>(
            model->environment, config.model_path.string().c_str(), model->session_options);
        const std::vector<std::string> inputs = tensorNames(*model->session, true);
        const std::vector<std::string> outputs = tensorNames(*model->session, false);
        if (!containsName(inputs, "image") || !containsName(inputs, "im_shape") ||
            !containsName(inputs, "scale_factor") || !containsName(outputs, "fetch_name_0") ||
            !containsName(outputs, "fetch_name_1") ||
            !matchesShape(*model->session, "image", {1, 3, config.input_height, config.input_width}) ||
            !matchesShape(*model->session, "im_shape", {1, 2}) ||
            !matchesShape(*model->session, "scale_factor", {1, 2})) {
            return nullptr;
        }
        model->image_input_name = "image";
        model->image_shape_input_name = "im_shape";
        model->scale_factor_input_name = "scale_factor";
        model->boxes_output_name = "fetch_name_0";
        model->box_count_output_name = "fetch_name_1";
        return model;
    } catch (const Ort::Exception& error) {
        if (envFlag(kDebugEnv)) {
            std::cerr << "[paddle-layout] failed to load model: " << error.what() << '\n';
        }
        return nullptr;
    }
}

PaddleDocLayoutOnnxBackend::PaddleDocLayoutOnnxBackend() : PaddleDocLayoutOnnxBackend(configFromEnvironment()) {}

PaddleDocLayoutOnnxBackend::PaddleDocLayoutOnnxBackend(PaddleDocLayoutOnnxConfig config)
    : config_(std::move(config)), model_(loadModel(config_)) {}

PaddleDocLayoutOnnxBackend::~PaddleDocLayoutOnnxBackend() = default;

PaddleDocLayoutOnnxBackend::PaddleDocLayoutOnnxBackend(PaddleDocLayoutOnnxBackend&&) noexcept = default;

PaddleDocLayoutOnnxBackend& PaddleDocLayoutOnnxBackend::operator=(PaddleDocLayoutOnnxBackend&&) noexcept = default;

bool PaddleDocLayoutOnnxBackend::isAvailable() const { return model_ != nullptr; }

const PaddleDocLayoutOnnxConfig& PaddleDocLayoutOnnxBackend::config() const { return config_; }

bool PaddleDocLayoutOnnxBackend::analyze(const LayoutRequest& request, LayoutResult& result) const {
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
        std::vector<Ort::Value> tensors;
        tensors.push_back(Ort::Value::CreateTensor<float>(memory_info,
                                                          input.resized_shape.data(),
                                                          input.resized_shape.size(),
                                                          input.pair_shape.data(),
                                                          input.pair_shape.size()));
        tensors.push_back(Ort::Value::CreateTensor<float>(
            memory_info, input.image.data(), input.image.size(), input.image_shape.data(), input.image_shape.size()));
        tensors.push_back(Ort::Value::CreateTensor<float>(memory_info,
                                                          input.scale_factor.data(),
                                                          input.scale_factor.size(),
                                                          input.pair_shape.data(),
                                                          input.pair_shape.size()));
        const std::array<const char*, 3> input_names{
            model_->image_shape_input_name.c_str(),
            model_->image_input_name.c_str(),
            model_->scale_factor_input_name.c_str(),
        };
        const std::array<const char*, 2> output_names{
            model_->boxes_output_name.c_str(),
            model_->box_count_output_name.c_str(),
        };
        std::vector<Ort::Value> outputs = model_->session->Run(Ort::RunOptions{nullptr},
                                                               input_names.data(),
                                                               tensors.data(),
                                                               tensors.size(),
                                                               output_names.data(),
                                                               output_names.size());
        std::size_t raw_count = 0;
        if (outputs.size() != output_names.size() ||
            !decode(
                outputs[0], outputs[1], image.size(), config_.confidence_threshold, result.layout.blocks, raw_count)) {
            return false;
        }

        for (std::size_t index = 0; index < result.layout.blocks.size(); ++index) {
            result.layout.blocks[index].id = blockId(request.page.page_number, index);
        }
        detail::assignTextLines(request.text, result.layout.blocks);
        detail::associateCaptions(request.page, result.layout.blocks);
        if (debug) {
            std::size_t linked_captions = 0;
            for (const document::LayoutBlock& block : result.layout.blocks) {
                linked_captions += block.related_block_id.empty() ? 0U : 1U;
            }
            std::cerr << "[paddle-layout] image=" << image.cols << 'x' << image.rows << " input=" << config_.input_width
                      << 'x' << config_.input_height << " threshold=" << config_.confidence_threshold
                      << " raw_boxes=" << raw_count << " filtered_blocks=" << result.layout.blocks.size()
                      << " linked_captions=" << linked_captions << '\n';
        }
        return true;
    } catch (const Ort::Exception& error) {
        if (debug) {
            std::cerr << "[paddle-layout] ONNX Runtime exception: " << error.what() << '\n';
        }
    } catch (const cv::Exception& error) {
        if (debug) {
            std::cerr << "[paddle-layout] OpenCV exception: " << error.what() << '\n';
        }
    }
    return false;
}

} // namespace doc_parser::layout
