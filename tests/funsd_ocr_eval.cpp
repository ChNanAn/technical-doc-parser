#include "document/page_artifact.h"
#include "ocr/paddle_ocr_onnx_backend.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path funsd_root;
    std::string split = "testing_data";
    std::size_t limit = 0;
    int dpi = 200;
    double detection_coverage_threshold = 0.5;
    std::filesystem::path report_path;
};

struct Word {
    std::string text;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
};

struct PageMetric {
    std::string image;
    std::size_t gt_chars = 0;
    std::size_t pred_chars = 0;
    std::size_t pred_lines = 0;
    std::size_t edit_distance = 0;
    double cer = 0.0;
    bool ok = false;
    std::size_t gt_words = 0;
    std::size_t detected_regions = 0;
    std::size_t detected_gt_words = 0;
    double detection_recall = 0.0;
    bool detection_ok = false;
    std::size_t gt_crop_chars = 0;
    std::size_t gt_crop_pred_chars = 0;
    std::size_t gt_crop_edit_distance = 0;
    std::size_t gt_crop_nonempty = 0;
    double gt_crop_cer = 0.0;
    bool gt_crop_recognition_ok = false;
};

void printUsage(const char* program) {
    std::cerr << "Usage: " << program
              << " --funsd-root <data/raw/funsd/dataset> [--split testing_data] [--limit N] "
                 "[--detection-coverage-threshold 0.5] [--report report.json]\n";
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        const auto require_value = [&](const char* name) -> const char* {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for " << name << '\n';
                return nullptr;
            }
            return argv[++index];
        };

        if (arg == "--funsd-root") {
            const char* value = require_value("--funsd-root");
            if (value == nullptr) {
                return false;
            }
            options.funsd_root = value;
        } else if (arg == "--split") {
            const char* value = require_value("--split");
            if (value == nullptr) {
                return false;
            }
            options.split = value;
        } else if (arg == "--limit") {
            const char* value = require_value("--limit");
            if (value == nullptr) {
                return false;
            }
            options.limit = static_cast<std::size_t>(std::stoul(value));
        } else if (arg == "--dpi") {
            const char* value = require_value("--dpi");
            if (value == nullptr) {
                return false;
            }
            options.dpi = std::stoi(value);
        } else if (arg == "--report") {
            const char* value = require_value("--report");
            if (value == nullptr) {
                return false;
            }
            options.report_path = value;
        } else if (arg == "--detection-coverage-threshold") {
            const char* value = require_value("--detection-coverage-threshold");
            if (value == nullptr) {
                return false;
            }
            options.detection_coverage_threshold = std::stod(value);
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            return false;
        }
    }

    if (options.funsd_root.empty()) {
        std::cerr << "--funsd-root is required\n";
        return false;
    }
    if (options.dpi <= 0) {
        std::cerr << "--dpi must be positive\n";
        return false;
    }
    if (options.detection_coverage_threshold <= 0.0 || options.detection_coverage_threshold > 1.0) {
        std::cerr << "--detection-coverage-threshold must be in (0, 1]\n";
        return false;
    }
    return true;
}

std::filesystem::path splitRoot(const Options& options) {
    const std::filesystem::path direct = options.funsd_root / options.split;
    if (std::filesystem::exists(direct)) {
        return direct;
    }

    const std::filesystem::path nested = options.funsd_root / "dataset" / options.split;
    if (std::filesystem::exists(nested)) {
        return nested;
    }

    return direct;
}

std::vector<std::filesystem::path> annotationFiles(const std::filesystem::path& root) {
    const std::filesystem::path annotations = root / "annotations";
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(annotations)) {
        return files;
    }

    for (const auto& entry : std::filesystem::directory_iterator(annotations)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::filesystem::path imageForAnnotation(const std::filesystem::path& split_root,
                                         const std::filesystem::path& annotation) {
    const std::filesystem::path images = split_root / "images";
    const std::string stem = annotation.stem().string();
    for (const char* extension : {".png", ".jpg", ".jpeg"}) {
        const std::filesystem::path candidate = images / (stem + extension);
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return images / (stem + ".png");
}

std::vector<Word> loadWords(const std::filesystem::path& annotation) {
    std::ifstream input(annotation);
    const nlohmann::json json = nlohmann::json::parse(input);

    std::vector<Word> words;
    for (const auto& item : json.value("form", nlohmann::json::array())) {
        for (const auto& word_json : item.value("words", nlohmann::json::array())) {
            const auto box = word_json.value("box", std::vector<int>{});
            if (box.size() < 4) {
                continue;
            }
            const std::string text = word_json.value("text", "");
            if (!text.empty()) {
                words.push_back({text, box[0], box[1], box[2], box[3]});
            }
        }
    }

    std::stable_sort(words.begin(), words.end(), [](const Word& lhs, const Word& rhs) {
        if (lhs.y0 != rhs.y0) {
            return lhs.y0 < rhs.y0;
        }
        return lhs.x0 < rhs.x0;
    });
    for (std::size_t index = 1; index < words.size(); ++index) {
        std::size_t current = index;
        while (current > 0 && std::abs(words[current].y0 - words[current - 1].y0) <= 8 &&
               words[current].x0 < words[current - 1].x0) {
            std::swap(words[current], words[current - 1]);
            --current;
        }
    }
    return words;
}

std::string joinWords(const std::vector<Word>& words) {
    std::string text;
    for (const auto& word : words) {
        if (!text.empty()) {
            text += ' ';
        }
        text += word.text;
    }
    return text;
}

std::string joinPredictedText(const doc_parser::document::PageText& page_text) {
    std::string text;
    for (const auto& line : page_text.lines) {
        if (!text.empty()) {
            text += ' ';
        }
        text += line.text;
    }
    return text;
}

std::string normalizeText(const std::string& value) {
    std::string normalized;
    bool previous_space = true;
    for (const unsigned char c : value) {
        if (std::isspace(c)) {
            if (!previous_space) {
                normalized += ' ';
            }
            previous_space = true;
        } else {
            normalized += static_cast<char>(std::tolower(c));
            previous_space = false;
        }
    }
    if (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }
    return normalized;
}

std::vector<unsigned int> utf8Codepoints(const std::string& text) {
    std::vector<unsigned int> codepoints;
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[index]);
        if ((c & 0x80U) == 0U) {
            codepoints.push_back(c);
            ++index;
        } else if ((c & 0xE0U) == 0xC0U && index + 1 < text.size()) {
            codepoints.push_back(((c & 0x1FU) << 6U) | (static_cast<unsigned char>(text[index + 1]) & 0x3FU));
            index += 2;
        } else if ((c & 0xF0U) == 0xE0U && index + 2 < text.size()) {
            codepoints.push_back(((c & 0x0FU) << 12U) | ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 6U) |
                                 (static_cast<unsigned char>(text[index + 2]) & 0x3FU));
            index += 3;
        } else if ((c & 0xF8U) == 0xF0U && index + 3 < text.size()) {
            codepoints.push_back(((c & 0x07U) << 18U) | ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 12U) |
                                 ((static_cast<unsigned char>(text[index + 2]) & 0x3FU) << 6U) |
                                 (static_cast<unsigned char>(text[index + 3]) & 0x3FU));
            index += 4;
        } else {
            codepoints.push_back(c);
            ++index;
        }
    }
    return codepoints;
}

std::size_t editDistance(const std::vector<unsigned int>& lhs, const std::vector<unsigned int>& rhs) {
    std::vector<std::size_t> previous(rhs.size() + 1);
    std::vector<std::size_t> current(rhs.size() + 1);

    for (std::size_t index = 0; index <= rhs.size(); ++index) {
        previous[index] = index;
    }

    for (std::size_t lhs_index = 1; lhs_index <= lhs.size(); ++lhs_index) {
        current[0] = lhs_index;
        for (std::size_t rhs_index = 1; rhs_index <= rhs.size(); ++rhs_index) {
            const std::size_t substitution = lhs[lhs_index - 1] == rhs[rhs_index - 1] ? 0 : 1;
            current[rhs_index] = std::min({
                previous[rhs_index] + 1,
                current[rhs_index - 1] + 1,
                previous[rhs_index - 1] + substitution,
            });
        }
        previous.swap(current);
    }

    return previous.back();
}

double wordCoverage(const Word& word, const doc_parser::document::BBox& detected) {
    const double intersection_width = std::max(
        0.0, std::min(static_cast<double>(word.x1), detected.x1) - std::max(static_cast<double>(word.x0), detected.x0));
    const double intersection_height = std::max(
        0.0, std::min(static_cast<double>(word.y1), detected.y1) - std::max(static_cast<double>(word.y0), detected.y0));
    const double word_area = static_cast<double>(std::max(0, word.x1 - word.x0) * std::max(0, word.y1 - word.y0));
    return word_area <= 0.0 ? 0.0 : intersection_width * intersection_height / word_area;
}

PageMetric evaluatePage(const doc_parser::ocr::PaddleOcrOnnxBackend& backend,
                        const std::filesystem::path& split_root,
                        const std::filesystem::path& annotation,
                        int dpi,
                        double detection_coverage_threshold) {
    PageMetric metric;
    const std::filesystem::path image_path = imageForAnnotation(split_root, annotation);
    metric.image = image_path.filename().string();

    const std::vector<Word> words = loadWords(annotation);
    metric.gt_words = words.size();
    const std::string gt = normalizeText(joinWords(words));
    const std::vector<unsigned int> gt_codepoints = utf8Codepoints(gt);
    metric.gt_chars = gt_codepoints.size();

    doc_parser::document::PageArtifact page;
    page.page_index = 0;
    page.page_number = 1;
    page.output_path = image_path;

    doc_parser::ocr::OcrDetectionResult detection_result;
    metric.detection_ok = backend.detect({page, dpi}, detection_result);
    metric.detected_regions = detection_result.regions.size();
    if (metric.detection_ok) {
        for (const Word& word : words) {
            const bool detected = std::any_of(detection_result.regions.begin(),
                                              detection_result.regions.end(),
                                              [&](const doc_parser::ocr::OcrRegion& region) {
                                                  return wordCoverage(word, region.bbox) >=
                                                         detection_coverage_threshold;
                                              });
            metric.detected_gt_words += detected ? 1U : 0U;
        }
    }
    metric.detection_recall =
        metric.gt_words == 0 ? 0.0
                             : static_cast<double>(metric.detected_gt_words) / static_cast<double>(metric.gt_words);

    std::vector<doc_parser::document::BBox> gt_regions;
    gt_regions.reserve(words.size());
    for (const Word& word : words) {
        gt_regions.push_back({static_cast<double>(word.x0),
                              static_cast<double>(word.y0),
                              static_cast<double>(word.x1),
                              static_cast<double>(word.y1)});
    }
    doc_parser::ocr::OcrRegionRecognitionResult gt_recognition_result;
    metric.gt_crop_recognition_ok = backend.recognizeRegions({page, dpi, gt_regions}, gt_recognition_result);
    for (std::size_t index = 0; index < words.size(); ++index) {
        const std::string reference = normalizeText(words[index].text);
        const std::string prediction = metric.gt_crop_recognition_ok && index < gt_recognition_result.regions.size()
                                           ? normalizeText(gt_recognition_result.regions[index].text)
                                           : "";
        const std::vector<unsigned int> reference_codepoints = utf8Codepoints(reference);
        const std::vector<unsigned int> prediction_codepoints = utf8Codepoints(prediction);
        metric.gt_crop_chars += reference_codepoints.size();
        metric.gt_crop_pred_chars += prediction_codepoints.size();
        metric.gt_crop_nonempty += prediction_codepoints.empty() ? 0U : 1U;
        metric.gt_crop_edit_distance += editDistance(prediction_codepoints, reference_codepoints);
    }
    metric.gt_crop_cer = metric.gt_crop_chars == 0 ? 0.0
                                                   : static_cast<double>(metric.gt_crop_edit_distance) /
                                                         static_cast<double>(metric.gt_crop_chars);

    doc_parser::ocr::OcrResult result;
    metric.ok = backend.recognize({page, dpi}, result);

    const std::string prediction = metric.ok ? normalizeText(joinPredictedText(result.page_text)) : "";
    const std::vector<unsigned int> prediction_codepoints = utf8Codepoints(prediction);
    metric.pred_chars = prediction_codepoints.size();
    metric.pred_lines = result.page_text.lines.size();
    metric.edit_distance = editDistance(prediction_codepoints, gt_codepoints);
    metric.cer = gt_codepoints.empty()
                     ? 0.0
                     : static_cast<double>(metric.edit_distance) / static_cast<double>(gt_codepoints.size());
    return metric;
}

void writeReport(const std::filesystem::path& path,
                 const std::vector<PageMetric>& metrics,
                 const doc_parser::ocr::PaddleOcrOnnxConfig& config,
                 double detection_coverage_threshold,
                 double corpus_cer,
                 double corpus_detection_recall,
                 double corpus_gt_crop_cer,
                 double ok_rate,
                 double text_found_rate) {
    nlohmann::json report;
    report["version"] = 2;
    report["model"] = {
        {"profile", config.profile.name},
        {"detection_model", config.detection_model.string()},
        {"recognition_model", config.recognition_model.string()},
        {"character_dict", config.character_dict.string()},
    };
    report["config"] = {
        {"detection_limit_side", config.detection_limit_side},
        {"detection_threshold", config.detection_threshold},
        {"box_threshold", config.box_threshold},
        {"unclip_ratio", config.unclip_ratio},
        {"recognition_batch_size", config.recognition_batch_size},
        {"recognition_base_width", config.recognition_base_width},
        {"recognition_max_width", config.recognition_max_width},
        {"recognition_threshold", config.recognition_threshold},
        {"detection_coverage_threshold", detection_coverage_threshold},
    };
    report["pages"] = metrics.size();
    report["ok_rate"] = ok_rate;
    report["text_found_rate"] = text_found_rate;
    report["corpus_cer"] = corpus_cer;
    report["detection_recall"] = corpus_detection_recall;
    report["gt_crop_recognition_cer"] = corpus_gt_crop_cer;
    report["items"] = nlohmann::json::array();
    for (const PageMetric& metric : metrics) {
        report["items"].push_back({
            {"image", metric.image},
            {"ok", metric.ok},
            {"gt_chars", metric.gt_chars},
            {"pred_chars", metric.pred_chars},
            {"pred_lines", metric.pred_lines},
            {"edit_distance", metric.edit_distance},
            {"cer", metric.cer},
            {"detection_ok", metric.detection_ok},
            {"gt_words", metric.gt_words},
            {"detected_regions", metric.detected_regions},
            {"detected_gt_words", metric.detected_gt_words},
            {"detection_recall", metric.detection_recall},
            {"gt_crop_recognition_ok", metric.gt_crop_recognition_ok},
            {"gt_crop_chars", metric.gt_crop_chars},
            {"gt_crop_pred_chars", metric.gt_crop_pred_chars},
            {"gt_crop_nonempty", metric.gt_crop_nonempty},
            {"gt_crop_edit_distance", metric.gt_crop_edit_distance},
            {"gt_crop_cer", metric.gt_crop_cer},
        });
    }

    std::ofstream output(path);
    output << report.dump(2) << '\n';
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        printUsage(argv[0]);
        return 2;
    }

    const std::filesystem::path root = splitRoot(options);
    std::vector<std::filesystem::path> annotations = annotationFiles(root);
    if (annotations.empty()) {
        std::cerr << "No FUNSD annotations found under " << root << "/annotations\n";
        return 2;
    }
    if (options.limit > 0 && annotations.size() > options.limit) {
        annotations.resize(options.limit);
    }

    const doc_parser::ocr::PaddleOcrOnnxBackend backend;
    if (!backend.isAvailable()) {
        std::cerr << "PaddleOCR ONNX backend is unavailable. Set model env vars documented in docs/dependencies.md\n";
        return 2;
    }

    std::vector<PageMetric> metrics;
    metrics.reserve(annotations.size());
    std::size_t total_edit_distance = 0;
    std::size_t total_gt_chars = 0;
    std::size_t ok_pages = 0;
    std::size_t text_found_pages = 0;
    std::size_t total_gt_words = 0;
    std::size_t total_detected_gt_words = 0;
    std::size_t total_gt_crop_chars = 0;
    std::size_t total_gt_crop_edit_distance = 0;

    for (const std::filesystem::path& annotation : annotations) {
        PageMetric metric = evaluatePage(backend, root, annotation, options.dpi, options.detection_coverage_threshold);
        total_edit_distance += metric.edit_distance;
        total_gt_chars += metric.gt_chars;
        ok_pages += metric.ok ? 1U : 0U;
        text_found_pages += metric.pred_chars > 0 ? 1U : 0U;
        total_gt_words += metric.gt_words;
        total_detected_gt_words += metric.detected_gt_words;
        total_gt_crop_chars += metric.gt_crop_chars;
        total_gt_crop_edit_distance += metric.gt_crop_edit_distance;
        std::cout << metric.image << " cer=" << metric.cer << " ok=" << (metric.ok ? "true" : "false")
                  << " gt_chars=" << metric.gt_chars << " pred_chars=" << metric.pred_chars
                  << " pred_lines=" << metric.pred_lines << " detection_recall=" << metric.detection_recall
                  << " gt_crop_cer=" << metric.gt_crop_cer << '\n';
        metrics.push_back(metric);
    }

    const double corpus_cer =
        total_gt_chars == 0 ? 0.0 : static_cast<double>(total_edit_distance) / static_cast<double>(total_gt_chars);
    const double ok_rate = metrics.empty() ? 0.0 : static_cast<double>(ok_pages) / static_cast<double>(metrics.size());
    const double text_found_rate =
        metrics.empty() ? 0.0 : static_cast<double>(text_found_pages) / static_cast<double>(metrics.size());
    const double corpus_detection_recall =
        total_gt_words == 0 ? 0.0 : static_cast<double>(total_detected_gt_words) / static_cast<double>(total_gt_words);
    const double corpus_gt_crop_cer = total_gt_crop_chars == 0 ? 0.0
                                                               : static_cast<double>(total_gt_crop_edit_distance) /
                                                                     static_cast<double>(total_gt_crop_chars);

    std::cout << "pages=" << metrics.size() << '\n';
    std::cout << "ok_rate=" << ok_rate << '\n';
    std::cout << "text_found_rate=" << text_found_rate << '\n';
    std::cout << "corpus_cer=" << corpus_cer << '\n';
    std::cout << "detection_recall=" << corpus_detection_recall << '\n';
    std::cout << "gt_crop_recognition_cer=" << corpus_gt_crop_cer << '\n';

    if (!options.report_path.empty()) {
        writeReport(options.report_path,
                    metrics,
                    backend.config(),
                    options.detection_coverage_threshold,
                    corpus_cer,
                    corpus_detection_recall,
                    corpus_gt_crop_cer,
                    ok_rate,
                    text_found_rate);
    }

    return 0;
}
