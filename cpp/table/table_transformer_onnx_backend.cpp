#include "table/table_backend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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

namespace doc_parser::table {

struct TableTransformerOnnxBackend::ModelBundle {
    ModelBundle() : environment(ORT_LOGGING_LEVEL_WARNING, "document-intelligence-engine-table-transformer") {
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
    }

    Ort::Env environment;
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> detection;
    std::unique_ptr<Ort::Session> structure;
};

namespace {

constexpr const char* kDetectionModelEnv = "DOCUMENT_INTELLIGENCE_ENGINE_TABLE_DETECTION_MODEL";
constexpr const char* kStructureModelEnv = "DOCUMENT_INTELLIGENCE_ENGINE_TABLE_STRUCTURE_MODEL";
constexpr const char* kDetectionConfidenceEnv = "DOCUMENT_INTELLIGENCE_ENGINE_TABLE_DETECTION_CONFIDENCE";
constexpr const char* kStructureConfidenceEnv = "DOCUMENT_INTELLIGENCE_ENGINE_TABLE_STRUCTURE_CONFIDENCE";
constexpr const char* kCropPaddingEnv = "DOCUMENT_INTELLIGENCE_ENGINE_TABLE_CROP_PADDING";
constexpr const char* kDebugEnv = "DOCUMENT_INTELLIGENCE_ENGINE_TABLE_DEBUG";

#ifndef DOC_PARSER_TABLE_DETECTION_MODEL_PATH
#define DOC_PARSER_TABLE_DETECTION_MODEL_PATH "models/table/table-transformer/detection.onnx"
#endif

#ifndef DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH
#define DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH "models/table/table-transformer/structure.onnx"
#endif

constexpr std::array<const char*, 2> kDetectionLabels{"table", "table rotated"};
constexpr std::array<const char*, 6> kStructureLabels{
    "table",
    "table column",
    "table row",
    "table column header",
    "table projected row header",
    "table spanning cell",
};

struct ModelInput {
    std::vector<float> tensor;
    std::array<int64_t, 4> shape{};
};

struct Prediction {
    std::string label;
    document::BBox bbox;
    double confidence = 0.0;
};

struct Token {
    std::string text;
    document::BBox bbox;
    double confidence = 1.0;
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

int envNonNegativeInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return fallback;
    }
    try {
        const int parsed = std::stoi(value);
        return parsed >= 0 ? parsed : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}

bool fileExists(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_regular_file(path, error);
}

TableTransformerOnnxConfig configFromEnvironment() {
    TableTransformerOnnxConfig config;
    const char* detection = std::getenv(kDetectionModelEnv);
    const char* structure = std::getenv(kStructureModelEnv);
    config.detection_model_path =
        detection == nullptr || std::string(detection).empty() ? DOC_PARSER_TABLE_DETECTION_MODEL_PATH : detection;
    config.structure_model_path =
        structure == nullptr || std::string(structure).empty() ? DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH : structure;
    config.detection_confidence_threshold =
        envProbability(kDetectionConfidenceEnv, config.detection_confidence_threshold);
    config.structure_confidence_threshold =
        envProbability(kStructureConfidenceEnv, config.structure_confidence_threshold);
    config.crop_padding = envNonNegativeInt(kCropPaddingEnv, config.crop_padding);
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

bool validateSession(const Ort::Session& session, int expected_classes) {
    const std::vector<std::string> inputs = tensorNames(session, true);
    const std::vector<std::string> outputs = tensorNames(session, false);
    if (inputs.size() != 1 || inputs.front() != "pixel_values" || outputs.size() != 2 ||
        std::find(outputs.begin(), outputs.end(), "logits") == outputs.end() ||
        std::find(outputs.begin(), outputs.end(), "pred_boxes") == outputs.end()) {
        return false;
    }
    const std::vector<int64_t> input_shape = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (input_shape.size() != 4 || (input_shape[1] > 0 && input_shape[1] != 3)) {
        return false;
    }
    const std::size_t logits_index = outputs.front() == "logits" ? 0U : 1U;
    const std::vector<int64_t> logits_shape =
        session.GetOutputTypeInfo(logits_index).GetTensorTypeAndShapeInfo().GetShape();
    return logits_shape.size() == 3 && (logits_shape[2] <= 0 || logits_shape[2] == expected_classes + 1);
}

double bboxArea(const document::BBox& bbox) {
    return std::max(0.0, bbox.x1 - bbox.x0) * std::max(0.0, bbox.y1 - bbox.y0);
}

double intersectionArea(const document::BBox& lhs, const document::BBox& rhs) {
    const double width = std::max(0.0, std::min(lhs.x1, rhs.x1) - std::max(lhs.x0, rhs.x0));
    const double height = std::max(0.0, std::min(lhs.y1, rhs.y1) - std::max(lhs.y0, rhs.y0));
    return width * height;
}

double intersectionOverUnion(const document::BBox& lhs, const document::BBox& rhs) {
    const double intersection = intersectionArea(lhs, rhs);
    const double area = bboxArea(lhs) + bboxArea(rhs) - intersection;
    return area <= 0.0 ? 0.0 : intersection / area;
}

document::BBox clipBBox(const document::BBox& bbox, const cv::Size& size) {
    return {
        std::clamp(bbox.x0, 0.0, static_cast<double>(size.width)),
        std::clamp(bbox.y0, 0.0, static_cast<double>(size.height)),
        std::clamp(bbox.x1, 0.0, static_cast<double>(size.width)),
        std::clamp(bbox.y1, 0.0, static_cast<double>(size.height)),
    };
}

document::BBox intersectBBox(const document::BBox& lhs, const document::BBox& rhs) {
    return {
        std::max(lhs.x0, rhs.x0),
        std::max(lhs.y0, rhs.y0),
        std::min(lhs.x1, rhs.x1),
        std::min(lhs.y1, rhs.y1),
    };
}

ModelInput makeInput(const cv::Mat& bgr_image, int shortest_edge, int longest_edge) {
    const double scale = std::min(static_cast<double>(shortest_edge) / std::min(bgr_image.rows, bgr_image.cols),
                                  static_cast<double>(longest_edge) / std::max(bgr_image.rows, bgr_image.cols));
    const int width = std::max(1, static_cast<int>(std::round(bgr_image.cols * scale)));
    const int height = std::max(1, static_cast<int>(std::round(bgr_image.rows * scale)));
    cv::Mat resized;
    cv::resize(bgr_image, resized, cv::Size(width, height), 0.0, 0.0, cv::INTER_LINEAR);
    cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
    resized.convertTo(resized, CV_32FC3, 1.0 / 255.0);

    constexpr std::array<float, 3> kMean{0.485F, 0.456F, 0.406F};
    constexpr std::array<float, 3> kStd{0.229F, 0.224F, 0.225F};
    ModelInput input;
    input.shape = {1, 3, height, width};
    input.tensor.resize(3U * static_cast<std::size_t>(height) * static_cast<std::size_t>(width));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const cv::Vec3f pixel = resized.at<cv::Vec3f>(y, x);
            for (int channel = 0; channel < 3; ++channel) {
                const std::size_t offset =
                    static_cast<std::size_t>(channel) * height * width + static_cast<std::size_t>(y) * width + x;
                input.tensor[offset] = (pixel[channel] - kMean[channel]) / kStd[channel];
            }
        }
    }
    return input;
}

template <std::size_t LabelCount>
bool runDetr(Ort::Session& session,
             const cv::Mat& image,
             int shortest_edge,
             int longest_edge,
             const std::array<const char*, LabelCount>& labels,
             double threshold,
             double offset_x,
             double offset_y,
             std::vector<Prediction>& predictions) {
    ModelInput input = makeInput(image, shortest_edge, longest_edge);
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(
        memory_info, input.tensor.data(), input.tensor.size(), input.shape.data(), input.shape.size());
    constexpr std::array<const char*, 1> kInputNames{"pixel_values"};
    constexpr std::array<const char*, 2> kOutputNames{"logits", "pred_boxes"};
    std::vector<Ort::Value> outputs =
        session.Run(Ort::RunOptions{nullptr}, kInputNames.data(), &tensor, 1, kOutputNames.data(), kOutputNames.size());
    if (outputs.size() != 2) {
        return false;
    }
    const std::vector<int64_t> logits_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    const std::vector<int64_t> boxes_shape = outputs[1].GetTensorTypeAndShapeInfo().GetShape();
    if (logits_shape.size() != 3 || boxes_shape.size() != 3 || logits_shape[0] != 1 || boxes_shape[0] != 1 ||
        logits_shape[1] <= 0 || logits_shape[2] != static_cast<int64_t>(LabelCount + 1) ||
        boxes_shape[1] != logits_shape[1] || boxes_shape[2] != 4) {
        return false;
    }
    const float* logits = outputs[0].GetTensorData<float>();
    const float* boxes = outputs[1].GetTensorData<float>();
    if (logits == nullptr || boxes == nullptr) {
        return false;
    }

    for (int64_t query = 0; query < logits_shape[1]; ++query) {
        const float* query_logits = logits + query * logits_shape[2];
        const float maximum = *std::max_element(query_logits, query_logits + logits_shape[2]);
        double denominator = 0.0;
        std::array<double, LabelCount + 1> probabilities{};
        for (std::size_t label = 0; label < probabilities.size(); ++label) {
            probabilities[label] = std::exp(static_cast<double>(query_logits[label] - maximum));
            denominator += probabilities[label];
        }
        const auto best = std::max_element(probabilities.begin(), probabilities.begin() + LabelCount);
        const std::size_t label_index = static_cast<std::size_t>(std::distance(probabilities.begin(), best));
        const double confidence = denominator <= 0.0 ? 0.0 : *best / denominator;
        if (confidence <= threshold) {
            continue;
        }

        const float* box = boxes + query * 4;
        const double center_x = box[0] * image.cols;
        const double center_y = box[1] * image.rows;
        const double width = box[2] * image.cols;
        const double height = box[3] * image.rows;
        document::BBox bbox{
            center_x - width * 0.5 + offset_x,
            center_y - height * 0.5 + offset_y,
            center_x + width * 0.5 + offset_x,
            center_y + height * 0.5 + offset_y,
        };
        if (bboxArea(bbox) > 0.0) {
            predictions.push_back({labels[label_index], bbox, confidence});
        }
    }
    return true;
}

void suppressDuplicateRegions(std::vector<Prediction>& regions) {
    std::stable_sort(regions.begin(), regions.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.confidence > rhs.confidence;
    });
    std::vector<Prediction> kept;
    for (const Prediction& region : regions) {
        const bool duplicate = std::any_of(kept.begin(), kept.end(), [&](const auto& candidate) {
            return intersectionOverUnion(region.bbox, candidate.bbox) > 0.8;
        });
        if (!duplicate) {
            kept.push_back(region);
        }
    }
    regions = std::move(kept);
}

std::string tableId(int page_number, std::size_t index) {
    std::ostringstream stream;
    stream << "page_" << page_number << "_table_" << index + 1;
    return stream.str();
}

std::string closestLayoutBlockId(const document::PageLayout& layout, const document::BBox& bbox) {
    double best_iou = 0.3;
    std::string id;
    for (const document::LayoutBlock& block : layout.blocks) {
        if (block.type != document::LayoutBlockType::Table) {
            continue;
        }
        const double iou = intersectionOverUnion(block.bbox, bbox);
        if (iou > best_iou) {
            best_iou = iou;
            id = block.id;
        }
    }
    return id;
}

std::vector<Token> collectTokens(const document::PageText& text) {
    std::vector<Token> tokens;
    for (const document::TextLine& line : text.lines) {
        if (line.spans.empty()) {
            if (!line.text.empty()) {
                tokens.push_back({line.text, line.bbox, line.confidence});
            }
            continue;
        }
        for (const document::TextSpan& span : line.spans) {
            if (!span.text.empty()) {
                tokens.push_back({span.text, span.bbox, span.confidence});
            }
        }
    }
    return tokens;
}

bool centerInside(const document::BBox& outer, const document::BBox& inner) {
    const double x = (inner.x0 + inner.x1) * 0.5;
    const double y = (inner.y0 + inner.y1) * 0.5;
    return x >= outer.x0 && x <= outer.x1 && y >= outer.y0 && y <= outer.y1;
}

void assignCellText(document::TableCell& cell, const std::vector<Token>& tokens) {
    std::vector<const Token*> matches;
    for (const Token& token : tokens) {
        if (centerInside(cell.bbox, token.bbox)) {
            matches.push_back(&token);
        }
    }
    std::stable_sort(matches.begin(), matches.end(), [](const Token* lhs, const Token* rhs) {
        const double lhs_center = (lhs->bbox.y0 + lhs->bbox.y1) * 0.5;
        const double rhs_center = (rhs->bbox.y0 + rhs->bbox.y1) * 0.5;
        if (std::abs(lhs_center - rhs_center) > 3.0) {
            return lhs_center < rhs_center;
        }
        return lhs->bbox.x0 < rhs->bbox.x0;
    });
    double confidence = 0.0;
    for (const Token* token : matches) {
        if (!cell.text.empty()) {
            cell.text += ' ';
        }
        cell.text += token->text;
        confidence += token->confidence;
    }
    if (!matches.empty()) {
        cell.confidence = std::min(cell.confidence, confidence / static_cast<double>(matches.size()));
    }
}

std::vector<int> rowsCoveredBy(const document::BBox& bbox, const std::vector<Prediction>& rows) {
    std::vector<int> indices;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const double center = (rows[index].bbox.y0 + rows[index].bbox.y1) * 0.5;
        if (center >= bbox.y0 - 1.0 && center <= bbox.y1 + 1.0) {
            indices.push_back(static_cast<int>(index));
        }
    }
    return indices;
}

std::vector<int> columnsCoveredBy(const document::BBox& bbox, const std::vector<Prediction>& columns) {
    std::vector<int> indices;
    for (std::size_t index = 0; index < columns.size(); ++index) {
        const double center = (columns[index].bbox.x0 + columns[index].bbox.x1) * 0.5;
        if (center >= bbox.x0 - 1.0 && center <= bbox.x1 + 1.0) {
            indices.push_back(static_cast<int>(index));
        }
    }
    return indices;
}

bool overlapsHeader(const document::BBox& bbox, const std::vector<Prediction>& headers) {
    return std::any_of(headers.begin(), headers.end(), [&](const Prediction& header) {
        const double area = bboxArea(bbox);
        return area > 0.0 && intersectionArea(bbox, header.bbox) / area > 0.5;
    });
}

void buildGrid(document::Table& table, const std::vector<Prediction>& structure, const std::vector<Token>& tokens) {
    std::vector<Prediction> rows;
    std::vector<Prediction> columns;
    std::vector<Prediction> headers;
    std::vector<Prediction> merged;
    for (Prediction object : structure) {
        object.bbox = intersectBBox(object.bbox, table.bbox);
        if (bboxArea(object.bbox) <= 0.0) {
            continue;
        }
        if (object.label == "table row") {
            rows.push_back(std::move(object));
        } else if (object.label == "table column") {
            columns.push_back(std::move(object));
        } else if (object.label == "table column header") {
            headers.push_back(std::move(object));
        } else if (object.label == "table spanning cell" || object.label == "table projected row header") {
            merged.push_back(std::move(object));
        }
    }
    std::stable_sort(
        rows.begin(), rows.end(), [](const auto& lhs, const auto& rhs) { return lhs.bbox.y0 < rhs.bbox.y0; });
    std::stable_sort(
        columns.begin(), columns.end(), [](const auto& lhs, const auto& rhs) { return lhs.bbox.x0 < rhs.bbox.x0; });
    if (rows.empty() || columns.empty()) {
        return;
    }

    for (std::size_t column_index = 0; column_index < columns.size(); ++column_index) {
        table.columns.push_back(
            {static_cast<int>(column_index), columns[column_index].bbox, columns[column_index].confidence});
    }
    table.rows.reserve(rows.size());
    for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
        document::TableRow row;
        row.row_index = static_cast<int>(row_index);
        row.bbox = rows[row_index].bbox;
        row.confidence = rows[row_index].confidence;
        row.is_header = overlapsHeader(row.bbox, headers);
        table.rows.push_back(std::move(row));
    }

    std::vector<std::vector<bool>> covered(rows.size(), std::vector<bool>(columns.size(), false));
    for (const Prediction& object : merged) {
        const std::vector<int> covered_rows = rowsCoveredBy(object.bbox, rows);
        const std::vector<int> covered_columns = columnsCoveredBy(object.bbox, columns);
        if (covered_rows.empty() || covered_columns.empty()) {
            continue;
        }
        document::TableCell cell;
        cell.row_index = covered_rows.front();
        cell.column_index = covered_columns.front();
        cell.row_span = static_cast<int>(covered_rows.size());
        cell.column_span = static_cast<int>(covered_columns.size());
        cell.is_header = object.label == "table projected row header" || overlapsHeader(object.bbox, headers);
        cell.bbox = object.bbox;
        cell.confidence = object.confidence;
        assignCellText(cell, tokens);
        table.rows[static_cast<std::size_t>(cell.row_index)].cells.push_back(std::move(cell));
        for (const int row : covered_rows) {
            for (const int column : covered_columns) {
                covered[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = true;
            }
        }
    }

    for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
        for (std::size_t column_index = 0; column_index < columns.size(); ++column_index) {
            if (covered[row_index][column_index]) {
                continue;
            }
            document::TableCell cell;
            cell.row_index = static_cast<int>(row_index);
            cell.column_index = static_cast<int>(column_index);
            cell.is_header = table.rows[row_index].is_header;
            cell.bbox = intersectBBox(rows[row_index].bbox, columns[column_index].bbox);
            cell.confidence = std::min(rows[row_index].confidence, columns[column_index].confidence);
            assignCellText(cell, tokens);
            table.rows[row_index].cells.push_back(std::move(cell));
        }
        std::stable_sort(table.rows[row_index].cells.begin(),
                         table.rows[row_index].cells.end(),
                         [](const auto& lhs, const auto& rhs) { return lhs.column_index < rhs.column_index; });
    }
}

cv::Rect expandedCrop(const document::BBox& bbox, const cv::Size& size, int padding) {
    const int x0 = std::max(0, static_cast<int>(std::floor(bbox.x0)) - padding);
    const int y0 = std::max(0, static_cast<int>(std::floor(bbox.y0)) - padding);
    const int x1 = std::min(size.width, static_cast<int>(std::ceil(bbox.x1)) + padding);
    const int y1 = std::min(size.height, static_cast<int>(std::ceil(bbox.y1)) + padding);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

} // namespace

std::unique_ptr<TableTransformerOnnxBackend::ModelBundle>
TableTransformerOnnxBackend::loadModels(const TableTransformerOnnxConfig& config) {
    if (!fileExists(config.detection_model_path) || !fileExists(config.structure_model_path) ||
        config.detection_confidence_threshold <= 0.0 || config.detection_confidence_threshold >= 1.0 ||
        config.structure_confidence_threshold <= 0.0 || config.structure_confidence_threshold >= 1.0 ||
        config.crop_padding < 0) {
        return nullptr;
    }
    try {
        auto models = std::make_unique<ModelBundle>();
        models->detection = std::make_unique<Ort::Session>(
            models->environment, config.detection_model_path.string().c_str(), models->session_options);
        models->structure = std::make_unique<Ort::Session>(
            models->environment, config.structure_model_path.string().c_str(), models->session_options);
        if (!validateSession(*models->detection, static_cast<int>(kDetectionLabels.size())) ||
            !validateSession(*models->structure, static_cast<int>(kStructureLabels.size()))) {
            return nullptr;
        }
        return models;
    } catch (const Ort::Exception& error) {
        if (envFlag(kDebugEnv)) {
            std::cerr << "[table-transformer] failed to load models: " << error.what() << '\n';
        }
        return nullptr;
    }
}

TableTransformerOnnxBackend::TableTransformerOnnxBackend() : TableTransformerOnnxBackend(configFromEnvironment()) {}

TableTransformerOnnxBackend::TableTransformerOnnxBackend(TableTransformerOnnxConfig config)
    : config_(std::move(config)), models_(loadModels(config_)) {}

TableTransformerOnnxBackend::~TableTransformerOnnxBackend() = default;

TableTransformerOnnxBackend::TableTransformerOnnxBackend(TableTransformerOnnxBackend&&) noexcept = default;

TableTransformerOnnxBackend& TableTransformerOnnxBackend::operator=(TableTransformerOnnxBackend&&) noexcept = default;

bool TableTransformerOnnxBackend::isAvailable() const { return models_ != nullptr; }

const TableTransformerOnnxConfig& TableTransformerOnnxBackend::config() const { return config_; }

bool TableTransformerOnnxBackend::recognize(const TableRequest& request, TableResult& result) const {
    result = {};
    result.tables.page_index = request.page.page_index;
    result.tables.page_number = request.page.page_number;
    const bool debug = envFlag(kDebugEnv);
    if (models_ == nullptr || !fileExists(request.page.output_path)) {
        return false;
    }
    const cv::Mat image = cv::imread(request.page.output_path.string(), cv::IMREAD_COLOR);
    if (image.empty()) {
        return false;
    }

    try {
        std::vector<Prediction> regions;
        if (!runDetr(*models_->detection,
                     image,
                     800,
                     800,
                     kDetectionLabels,
                     config_.detection_confidence_threshold,
                     0.0,
                     0.0,
                     regions)) {
            return false;
        }
        regions.erase(std::remove_if(regions.begin(),
                                     regions.end(),
                                     [&](Prediction& region) {
                                         region.bbox = clipBBox(region.bbox, image.size());
                                         return region.label != "table" || bboxArea(region.bbox) <= 0.0;
                                     }),
                      regions.end());
        const std::size_t detected_regions = regions.size();
        if (regions.empty()) {
            for (const document::LayoutBlock& block : request.layout.blocks) {
                if (block.type == document::LayoutBlockType::Table && bboxArea(block.bbox) > 0.0) {
                    regions.push_back({"layout table fallback", clipBBox(block.bbox, image.size()), block.confidence});
                }
            }
        }
        suppressDuplicateRegions(regions);
        const std::vector<Token> tokens = collectTokens(request.text);

        for (const Prediction& region : regions) {
            const cv::Rect crop_rect = expandedCrop(region.bbox, image.size(), config_.crop_padding);
            if (crop_rect.width <= 0 || crop_rect.height <= 0) {
                continue;
            }
            std::vector<Prediction> structure;
            const cv::Mat crop = image(crop_rect);
            if (!runDetr(*models_->structure,
                         crop,
                         800,
                         1000,
                         kStructureLabels,
                         config_.structure_confidence_threshold,
                         crop_rect.x,
                         crop_rect.y,
                         structure)) {
                return false;
            }

            document::Table table;
            table.id = tableId(request.page.page_number, result.tables.tables.size());
            table.layout_block_id = closestLayoutBlockId(request.layout, region.bbox);
            table.page_index = request.page.page_index;
            table.page_number = request.page.page_number;
            table.bbox = region.bbox;
            table.confidence = region.confidence;
            table.source_label = region.label;
            table.structure_objects.push_back({"table", region.bbox, region.confidence});
            for (const Prediction& object : structure) {
                if (object.label != "table") {
                    table.structure_objects.push_back({object.label, object.bbox, object.confidence});
                }
            }
            buildGrid(table, structure, tokens);
            result.tables.tables.push_back(std::move(table));
        }

        if (debug) {
            std::size_t rows = 0;
            std::size_t columns = 0;
            std::size_t cells = 0;
            std::size_t merged_cells = 0;
            for (const document::Table& table : result.tables.tables) {
                rows += table.rows.size();
                columns += table.columns.size();
                for (const document::TableRow& row : table.rows) {
                    cells += row.cells.size();
                    merged_cells += static_cast<std::size_t>(
                        std::count_if(row.cells.begin(), row.cells.end(), [](const auto& cell) {
                            return cell.row_span > 1 || cell.column_span > 1;
                        }));
                }
            }
            std::cerr << "[table-transformer] image=" << image.cols << 'x' << image.rows
                      << " detection_threshold=" << config_.detection_confidence_threshold
                      << " structure_threshold=" << config_.structure_confidence_threshold
                      << " crop_padding=" << config_.crop_padding << " detected_regions=" << detected_regions
                      << " output_tables=" << result.tables.tables.size() << " rows=" << rows << " columns=" << columns
                      << " cells=" << cells << " merged_cells=" << merged_cells << '\n';
        }
        return true;
    } catch (const Ort::Exception& error) {
        if (debug) {
            std::cerr << "[table-transformer] ONNX Runtime exception: " << error.what() << '\n';
        }
    } catch (const cv::Exception& error) {
        if (debug) {
            std::cerr << "[table-transformer] OpenCV exception: " << error.what() << '\n';
        }
    }
    return false;
}

} // namespace doc_parser::table
