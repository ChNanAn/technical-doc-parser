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
    std::filesystem::path report_path;
};

struct Word {
    std::string text;
    int x0 = 0;
    int y0 = 0;
};

struct PageMetric {
    std::string image;
    std::size_t gt_chars = 0;
    std::size_t pred_chars = 0;
    std::size_t pred_lines = 0;
    std::size_t edit_distance = 0;
    double cer = 0.0;
    bool ok = false;
};

void printUsage(const char* program) {
    std::cerr << "Usage: " << program
              << " --funsd-root <data/raw/funsd/dataset> [--split testing_data] [--limit N] "
                 "[--report report.json]\n";
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
            if (box.size() < 2) {
                continue;
            }
            const std::string text = word_json.value("text", "");
            if (!text.empty()) {
                words.push_back({text, box[0], box[1]});
            }
        }
    }

    std::sort(words.begin(), words.end(), [](const Word& lhs, const Word& rhs) {
        if (std::abs(lhs.y0 - rhs.y0) > 8) {
            return lhs.y0 < rhs.y0;
        }
        return lhs.x0 < rhs.x0;
    });
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

PageMetric evaluatePage(const doc_parser::ocr::PaddleOcrOnnxBackend& backend,
                        const std::filesystem::path& split_root,
                        const std::filesystem::path& annotation,
                        int dpi) {
    PageMetric metric;
    const std::filesystem::path image_path = imageForAnnotation(split_root, annotation);
    metric.image = image_path.filename().string();

    const std::string gt = normalizeText(joinWords(loadWords(annotation)));
    const std::vector<unsigned int> gt_codepoints = utf8Codepoints(gt);
    metric.gt_chars = gt_codepoints.size();

    doc_parser::document::PageArtifact page;
    page.page_index = 0;
    page.page_number = 1;
    page.output_path = image_path;

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
                 double corpus_cer,
                 double ok_rate,
                 double text_found_rate) {
    nlohmann::json report;
    report["pages"] = metrics.size();
    report["ok_rate"] = ok_rate;
    report["text_found_rate"] = text_found_rate;
    report["corpus_cer"] = corpus_cer;
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

    for (const std::filesystem::path& annotation : annotations) {
        PageMetric metric = evaluatePage(backend, root, annotation, options.dpi);
        total_edit_distance += metric.edit_distance;
        total_gt_chars += metric.gt_chars;
        ok_pages += metric.ok ? 1U : 0U;
        text_found_pages += metric.pred_chars > 0 ? 1U : 0U;
        std::cout << metric.image << " cer=" << metric.cer << " ok=" << (metric.ok ? "true" : "false")
                  << " gt_chars=" << metric.gt_chars << " pred_chars=" << metric.pred_chars
                  << " pred_lines=" << metric.pred_lines << '\n';
        metrics.push_back(metric);
    }

    const double corpus_cer =
        total_gt_chars == 0 ? 0.0 : static_cast<double>(total_edit_distance) / static_cast<double>(total_gt_chars);
    const double ok_rate = metrics.empty() ? 0.0 : static_cast<double>(ok_pages) / static_cast<double>(metrics.size());
    const double text_found_rate =
        metrics.empty() ? 0.0 : static_cast<double>(text_found_pages) / static_cast<double>(metrics.size());

    std::cout << "pages=" << metrics.size() << '\n';
    std::cout << "ok_rate=" << ok_rate << '\n';
    std::cout << "text_found_rate=" << text_found_rate << '\n';
    std::cout << "corpus_cer=" << corpus_cer << '\n';

    if (!options.report_path.empty()) {
        writeReport(options.report_path, metrics, corpus_cer, ok_rate, text_found_rate);
    }

    return 0;
}
