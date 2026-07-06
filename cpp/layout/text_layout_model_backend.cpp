#include "layout/layout_backend.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace doc_parser::layout {
namespace {

using document::BBox;
using document::LayoutBlock;
using document::LayoutBlockType;

struct LineCandidate {
    int index = 0;
    std::string text;
    BBox bbox;
    double height = 0.0;
};

double bboxWidth(const BBox& bbox) { return std::max(0.0, bbox.x1 - bbox.x0); }

double bboxHeight(const BBox& bbox) { return std::max(0.0, bbox.y1 - bbox.y0); }

void expandBBox(BBox& target, const BBox& value) {
    target.x0 = std::min(target.x0, value.x0);
    target.y0 = std::min(target.y0, value.y0);
    target.x1 = std::max(target.x1, value.x1);
    target.y1 = std::max(target.y1, value.y1);
}

std::string trim(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); });
    return std::string(first, last.base());
}

bool startsWithListMarker(const std::string& text) {
    const std::string value = trim(text);
    if (value.empty()) {
        return false;
    }
    if (value[0] == '-' || value[0] == '*' || value[0] == '+') {
        return value.size() > 1 && std::isspace(static_cast<unsigned char>(value[1]));
    }
    if (!std::isdigit(static_cast<unsigned char>(value[0]))) {
        return false;
    }

    std::size_t index = 1;
    while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index]))) {
        ++index;
    }
    return index + 1 < value.size() && (value[index] == '.' || value[index] == ')') &&
           std::isspace(static_cast<unsigned char>(value[index + 1]));
}

bool hasTableLikeSpanGaps(const document::TextLine& line) {
    if (line.spans.size() < 3) {
        return false;
    }

    std::vector<document::TextSpan> spans = line.spans;
    std::sort(spans.begin(), spans.end(), [](const auto& lhs, const auto& rhs) { return lhs.bbox.x0 < rhs.bbox.x0; });

    const double threshold = std::max(16.0, bboxHeight(line.bbox) * 1.8);
    int large_gaps = 0;
    for (std::size_t index = 1; index < spans.size(); ++index) {
        const double gap = spans[index].bbox.x0 - spans[index - 1].bbox.x1;
        if (gap > threshold) {
            ++large_gaps;
        }
    }
    return large_gaps >= 2;
}

bool hasRepeatedDotLeader(const std::string& text) {
    return text.find("...") != std::string::npos || text.find(" . . ") != std::string::npos;
}

double medianLineHeight(const std::vector<LineCandidate>& lines) {
    if (lines.empty()) {
        return 0.0;
    }

    std::vector<double> heights;
    heights.reserve(lines.size());
    for (const auto& line : lines) {
        if (line.height > 0.0) {
            heights.push_back(line.height);
        }
    }
    if (heights.empty()) {
        return 0.0;
    }

    std::sort(heights.begin(), heights.end());
    return heights[heights.size() / 2];
}

LayoutBlockType classifyLine(const document::PageArtifact& page,
                             const document::TextLine& line,
                             const LineCandidate& candidate,
                             double median_height,
                             bool first_body_line) {
    const double page_height = page.height > 0 ? static_cast<double>(page.height) : std::max(candidate.bbox.y1, 1.0);
    const double top_ratio = candidate.bbox.y0 / page_height;
    const double bottom_ratio = candidate.bbox.y1 / page_height;

    if (top_ratio < 0.08) {
        return LayoutBlockType::Header;
    }
    if (bottom_ratio > 0.92) {
        return LayoutBlockType::Footer;
    }
    if (hasTableLikeSpanGaps(line) || hasRepeatedDotLeader(candidate.text)) {
        return LayoutBlockType::Table;
    }
    if (startsWithListMarker(candidate.text)) {
        return LayoutBlockType::List;
    }
    if (first_body_line && candidate.text.size() <= 120 &&
        (median_height <= 0.0 || candidate.height >= median_height * 1.05)) {
        return LayoutBlockType::Title;
    }

    return LayoutBlockType::Text;
}

double blockConfidence(LayoutBlockType type, const LayoutBlock& block) {
    switch (type) {
    case LayoutBlockType::Title:
        return 0.70;
    case LayoutBlockType::Header:
    case LayoutBlockType::Footer:
        return 0.65;
    case LayoutBlockType::Table:
    case LayoutBlockType::List:
        return 0.60;
    case LayoutBlockType::Figure:
        return 0.55;
    case LayoutBlockType::Text:
        return block.text_line_indices.size() > 1 ? 0.75 : 0.68;
    case LayoutBlockType::Unknown:
        return 0.40;
    }
    return 0.40;
}

bool canMerge(const LayoutBlock& block,
              LayoutBlockType next_type,
              const LineCandidate& previous,
              const LineCandidate& next) {
    if (block.type != next_type) {
        return false;
    }
    if (next_type == LayoutBlockType::Title || next_type == LayoutBlockType::Header ||
        next_type == LayoutBlockType::Footer) {
        return false;
    }

    const double previous_height = std::max(1.0, previous.height);
    const double gap = next.bbox.y0 - previous.bbox.y1;
    const double left_delta = std::abs(next.bbox.x0 - previous.bbox.x0);
    return gap >= 0.0 && gap <= previous_height * 1.8 && left_delta <= previous_height * 4.0;
}

std::string blockId(int page_number, std::size_t block_index) {
    std::ostringstream stream;
    stream << "page_" << page_number << "_block_" << block_index + 1;
    return stream.str();
}

} // namespace

bool TextLayoutModelBackend::analyze(const LayoutRequest& request, LayoutResult& result) const {
    result.layout = {};
    result.layout.page_index = request.page.page_index;
    result.layout.page_number = request.page.page_number;

    std::vector<LineCandidate> candidates;
    candidates.reserve(request.text.lines.size());
    for (std::size_t index = 0; index < request.text.lines.size(); ++index) {
        const auto& line = request.text.lines[index];
        const std::string text = trim(line.text);
        if (text.empty() || bboxWidth(line.bbox) <= 0.0 || bboxHeight(line.bbox) <= 0.0) {
            continue;
        }
        candidates.push_back({
            static_cast<int>(index),
            text,
            line.bbox,
            bboxHeight(line.bbox),
        });
    }

    if (candidates.empty()) {
        if (request.page.width > 0 && request.page.height > 0) {
            LayoutBlock block;
            block.id = blockId(request.page.page_number, 0);
            block.type = LayoutBlockType::Figure;
            block.bbox = {0.0, 0.0, static_cast<double>(request.page.width), static_cast<double>(request.page.height)};
            block.confidence = blockConfidence(block.type, block);
            result.layout.blocks.push_back(block);
        }
        return true;
    }

    const double median_height = medianLineHeight(candidates);
    bool saw_body_line = false;
    LineCandidate previous_line;

    for (const auto& candidate : candidates) {
        const auto& source_line = request.text.lines[static_cast<std::size_t>(candidate.index)];
        const bool first_body_line = !saw_body_line &&
                                     candidate.bbox.y0 >= static_cast<double>(std::max(0, request.page.height)) * 0.08;
        const LayoutBlockType type = classifyLine(request.page, source_line, candidate, median_height, first_body_line);
        if (type != LayoutBlockType::Header && type != LayoutBlockType::Footer) {
            saw_body_line = true;
        }

        if (!result.layout.blocks.empty() && canMerge(result.layout.blocks.back(), type, previous_line, candidate)) {
            LayoutBlock& block = result.layout.blocks.back();
            expandBBox(block.bbox, candidate.bbox);
            block.text_line_indices.push_back(candidate.index);
            block.confidence = blockConfidence(block.type, block);
        } else {
            LayoutBlock block;
            block.id = blockId(request.page.page_number, result.layout.blocks.size());
            block.type = type;
            block.bbox = candidate.bbox;
            block.text_line_indices.push_back(candidate.index);
            block.confidence = blockConfidence(block.type, block);
            result.layout.blocks.push_back(block);
        }

        previous_line = candidate;
    }

    return true;
}

} // namespace doc_parser::layout
