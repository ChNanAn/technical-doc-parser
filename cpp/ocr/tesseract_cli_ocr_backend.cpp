#include "ocr/tesseract_cli_ocr_backend.h"

#include "document/text_model.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace doc_parser::ocr {
namespace {

constexpr const char* kTesseractCommandEnv = "DOCUMENT_INTELLIGENCE_ENGINE_TESSERACT_CMD";
constexpr const char* kTesseractLanguageEnv = "DOCUMENT_INTELLIGENCE_ENGINE_TESSERACT_LANG";
constexpr const char* kLegacyTesseractCommandEnv = "DOC_PARSER_TESSERACT_CMD";
constexpr const char* kLegacyTesseractLanguageEnv = "DOC_PARSER_TESSERACT_LANG";

struct TsvWord {
    int block = 0;
    int paragraph = 0;
    int line = 0;
    document::TextSpan span;
};

struct LineKey {
    int block = 0;
    int paragraph = 0;
    int line = 0;

    bool operator<(const LineKey& other) const {
        return std::tie(block, paragraph, line) < std::tie(other.block, other.paragraph, other.line);
    }
};

std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (const char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

std::string readCommandOutput(const std::string& command) {
    std::array<char, 4096> buffer{};
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return {};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);
    return output;
}

std::vector<std::string> splitTabLine(const std::string& line) {
    std::vector<std::string> columns;
    std::string column;
    std::stringstream stream(line);
    while (std::getline(stream, column, '\t')) {
        columns.push_back(column);
    }
    if (!line.empty() && line.back() == '\t') {
        columns.emplace_back();
    }
    return columns;
}

int parseInt(const std::string& value, int fallback = 0) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

double parseConfidence(const std::string& value) {
    try {
        const double confidence = std::stod(value);
        if (confidence < 0.0) {
            return 0.0;
        }
        return std::min(confidence / 100.0, 1.0);
    } catch (...) {
        return 0.0;
    }
}

document::BBox unionBBox(const document::BBox& lhs, const document::BBox& rhs) {
    return {
        std::min(lhs.x0, rhs.x0),
        std::min(lhs.y0, rhs.y0),
        std::max(lhs.x1, rhs.x1),
        std::max(lhs.y1, rhs.y1),
    };
}

std::vector<TsvWord> parseTsvWords(const std::string& tsv) {
    std::vector<TsvWord> words;
    std::stringstream stream(tsv);
    std::string line;
    bool header = true;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (header) {
            header = false;
            continue;
        }

        const std::vector<std::string> columns = splitTabLine(line);
        if (columns.size() < 12) {
            continue;
        }

        const std::string& text = columns[11];
        if (text.empty()) {
            continue;
        }

        const int confidence_raw = parseInt(columns[10], -1);
        if (confidence_raw < 0) {
            continue;
        }

        const int left = parseInt(columns[6]);
        const int top = parseInt(columns[7]);
        const int width = parseInt(columns[8]);
        const int height = parseInt(columns[9]);
        if (width <= 0 || height <= 0) {
            continue;
        }

        document::TextSpan span;
        span.text = text;
        span.bbox = {
            static_cast<double>(left),
            static_cast<double>(top),
            static_cast<double>(left + width),
            static_cast<double>(top + height),
        };
        span.source = document::TextSource::Ocr;
        span.confidence = parseConfidence(columns[10]);

        words.push_back({
            parseInt(columns[2]),
            parseInt(columns[3]),
            parseInt(columns[4]),
            span,
        });
    }

    return words;
}

void appendWordsAsLines(const std::vector<TsvWord>& words, document::PageText& page_text) {
    std::map<LineKey, std::vector<document::TextSpan>> grouped_words;
    for (const auto& word : words) {
        grouped_words[{word.block, word.paragraph, word.line}].push_back(word.span);
    }

    for (const auto& [key, spans] : grouped_words) {
        (void)key;
        if (spans.empty()) {
            continue;
        }

        document::TextLine line;
        line.source = document::TextSource::Ocr;
        line.confidence = 0.0;
        line.bbox = spans.front().bbox;

        for (std::size_t index = 0; index < spans.size(); ++index) {
            if (index > 0) {
                line.text += ' ';
            }
            line.text += spans[index].text;
            line.bbox = unionBBox(line.bbox, spans[index].bbox);
            line.confidence += spans[index].confidence;
            line.spans.push_back(spans[index]);
        }

        line.confidence /= static_cast<double>(spans.size());
        page_text.lines.push_back(line);
    }
}

std::string envOrDefault(const char* primary_name, const char* legacy_name, const std::string& fallback) {
    const char* value = std::getenv(primary_name);
    if (value != nullptr && !std::string(value).empty()) {
        return value;
    }

    value = std::getenv(legacy_name);
    if (value == nullptr || std::string(value).empty()) {
        return fallback;
    }
    return value;
}

} // namespace

TesseractCliOcrBackend::TesseractCliOcrBackend()
    : TesseractCliOcrBackend(envOrDefault(kTesseractCommandEnv, kLegacyTesseractCommandEnv, "tesseract"),
                             envOrDefault(kTesseractLanguageEnv, kLegacyTesseractLanguageEnv, "eng")) {}

TesseractCliOcrBackend::TesseractCliOcrBackend(std::string language)
    : TesseractCliOcrBackend(envOrDefault(kTesseractCommandEnv, kLegacyTesseractCommandEnv, "tesseract"),
                             std::move(language)) {}

TesseractCliOcrBackend::TesseractCliOcrBackend(std::string executable, std::string language)
    : executable_(std::move(executable)), language_(std::move(language)) {}

bool TesseractCliOcrBackend::isAvailable() const {
    const std::string command = "command -v " + shellQuote(executable_) + " >/dev/null 2>&1";
    return std::system(command.c_str()) == 0;
}

bool TesseractCliOcrBackend::recognize(const OcrRequest& request, OcrResult& result) const {
    result.page_text = {};
    result.page_text.page_index = request.page.page_index;
    result.page_text.page_number = request.page.page_number;
    result.page_text.preferred_source = document::TextSource::Unknown;

    if (request.dpi <= 0 || request.page.output_path.empty() || !std::filesystem::exists(request.page.output_path)) {
        return false;
    }

    const std::string command = shellQuote(executable_) + ' ' + shellQuote(request.page.output_path.string()) +
                                " stdout -l " + shellQuote(language_) + " --dpi " + std::to_string(request.dpi) +
                                " --psm 6 tsv 2>/dev/null";
    const std::string tsv = readCommandOutput(command);
    const std::vector<TsvWord> words = parseTsvWords(tsv);
    appendWordsAsLines(words, result.page_text);

    result.page_text.has_text = !result.page_text.lines.empty();
    result.page_text.preferred_source =
        result.page_text.has_text ? document::TextSource::Ocr : document::TextSource::Unknown;
    return true;
}

} // namespace doc_parser::ocr
