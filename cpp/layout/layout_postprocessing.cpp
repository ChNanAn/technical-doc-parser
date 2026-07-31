#include "layout/layout_postprocessing.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace doc_parser::layout::detail {
namespace {

double bboxArea(const document::BBox& bbox) {
    return std::max(0.0, bbox.x1 - bbox.x0) * std::max(0.0, bbox.y1 - bbox.y0);
}

double overlapOverLine(const document::BBox& line, const document::BBox& block) {
    const double width = std::max(0.0, std::min(line.x1, block.x1) - std::max(line.x0, block.x0));
    const double height = std::max(0.0, std::min(line.y1, block.y1) - std::max(line.y0, block.y0));
    const double area = bboxArea(line);
    return area <= 0.0 ? 0.0 : width * height / area;
}

bool containsCenter(const document::BBox& outer, const document::BBox& inner) {
    const double center_x = (inner.x0 + inner.x1) * 0.5;
    const double center_y = (inner.y0 + inner.y1) * 0.5;
    return center_x >= outer.x0 && center_x <= outer.x1 && center_y >= outer.y0 && center_y <= outer.y1;
}

double intervalDistance(double lhs_begin, double lhs_end, double rhs_begin, double rhs_end) {
    if (lhs_end < rhs_begin) {
        return rhs_begin - lhs_end;
    }
    if (rhs_end < lhs_begin) {
        return lhs_begin - rhs_end;
    }
    return 0.0;
}

double captionDistance(const document::BBox& caption, const document::BBox& target) {
    const double dx = intervalDistance(caption.x0, caption.x1, target.x0, target.x1);
    const double dy = intervalDistance(caption.y0, caption.y1, target.y0, target.y1);
    return std::hypot(dx, dy);
}

bool isCaptionLabel(const std::string& label) { return label == "Caption" || label == "figure_title"; }

bool hasVisibleText(const std::string& text) {
    return std::any_of(text.begin(), text.end(), [](unsigned char value) { return !std::isspace(value); });
}

bool validBBox(const document::BBox& bbox) { return bbox.x1 > bbox.x0 && bbox.y1 > bbox.y0; }

bool isFurniture(document::LayoutBlockType type) {
    return type == document::LayoutBlockType::Header || type == document::LayoutBlockType::Footer;
}

bool overlapsFurniture(const document::BBox& line_bbox, const std::vector<document::LayoutBlock>& blocks) {
    return std::any_of(blocks.begin(), blocks.end(), [&](const document::LayoutBlock& block) {
        return isFurniture(block.type) &&
               (overlapOverLine(line_bbox, block.bbox) >= 0.10 || containsCenter(block.bbox, line_bbox));
    });
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    const std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    const double upper = values[middle];
    if (values.size() % 2 != 0) {
        return upper;
    }
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle - 1), values.end());
    return (values[middle - 1] + upper) * 0.5;
}

double horizontalOverlap(const document::BBox& lhs, const document::BBox& rhs) {
    return std::max(0.0, std::min(lhs.x1, rhs.x1) - std::max(lhs.x0, rhs.x0));
}

struct LineColumn {
    document::BBox bbox;
    std::vector<int> line_indices;
};

struct FallbackLineGroup {
    document::BBox bbox;
    document::BBox last_line_bbox;
    std::vector<int> line_indices;
    std::string source_label;
};

bool supportsLineRefinement(document::LayoutBlockType type) {
    return type == document::LayoutBlockType::Unknown || type == document::LayoutBlockType::Text ||
           type == document::LayoutBlockType::List;
}

double intervalGap(double lhs_begin, double lhs_end, double rhs_begin, double rhs_end) {
    if (lhs_end < rhs_begin) {
        return rhs_begin - lhs_end;
    }
    if (rhs_end < lhs_begin) {
        return lhs_begin - rhs_end;
    }
    return 0.0;
}

bool canJoinFallbackGroup(const document::BBox& previous, const document::BBox& current) {
    const double previous_height = std::max(0.0, previous.y1 - previous.y0);
    const double current_height = std::max(0.0, current.y1 - current.y0);
    const double reference_height = std::max(previous_height, current_height);
    const double shorter_height = std::min(previous_height, current_height);
    if (reference_height <= 0.0 || shorter_height <= 0.0) {
        return false;
    }

    const double overlap_y = std::max(0.0, std::min(previous.y1, current.y1) - std::max(previous.y0, current.y0));
    if (overlap_y / shorter_height >= 0.50) {
        return intervalGap(previous.x0, previous.x1, current.x0, current.x1) <= reference_height * 0.75;
    }

    const double vertical_gap = current.y0 - previous.y1;
    if (vertical_gap < -reference_height * 0.25 || vertical_gap > reference_height * 1.60) {
        return false;
    }

    const double previous_width = std::max(0.0, previous.x1 - previous.x0);
    const double current_width = std::max(0.0, current.x1 - current.x0);
    const double overlap_x = horizontalOverlap(previous, current);
    const double overlap_ratio = overlap_x / std::max(1.0e-3, std::min(previous_width, current_width));
    const double left_edge_delta = std::abs(previous.x0 - current.x0);
    return overlap_ratio >= 0.25 || left_edge_delta <= reference_height * 2.5;
}

void expandBBox(document::BBox& destination, const document::BBox& source) {
    destination.x0 = std::min(destination.x0, source.x0);
    destination.y0 = std::min(destination.y0, source.y0);
    destination.x1 = std::max(destination.x1, source.x1);
    destination.y1 = std::max(destination.y1, source.y1);
}

void sortLineIndicesRowWise(const document::PageText& text, std::vector<int>& line_indices, double median_line_height) {
    std::stable_sort(line_indices.begin(), line_indices.end(), [&](int lhs, int rhs) {
        const document::BBox& left = text.lines[static_cast<std::size_t>(lhs)].bbox;
        const document::BBox& right = text.lines[static_cast<std::size_t>(rhs)].bbox;
        if (std::abs(left.y0 - right.y0) > 1.0e-3) {
            return left.y0 < right.y0;
        }
        return left.x0 < right.x0;
    });

    const double row_tolerance = std::max(1.0, median_line_height * 0.50);
    for (std::size_t row_begin = 0; row_begin < line_indices.size();) {
        const double row_y = text.lines[static_cast<std::size_t>(line_indices[row_begin])].bbox.y0;
        std::size_t row_end = row_begin + 1;
        while (row_end < line_indices.size()) {
            const double candidate_y = text.lines[static_cast<std::size_t>(line_indices[row_end])].bbox.y0;
            if (candidate_y - row_y > row_tolerance) {
                break;
            }
            ++row_end;
        }
        std::stable_sort(line_indices.begin() + static_cast<std::ptrdiff_t>(row_begin),
                         line_indices.begin() + static_cast<std::ptrdiff_t>(row_end),
                         [&](int lhs, int rhs) {
                             return text.lines[static_cast<std::size_t>(lhs)].bbox.x0 <
                                    text.lines[static_cast<std::size_t>(rhs)].bbox.x0;
                         });
        row_begin = row_end;
    }
}

std::vector<FallbackLineGroup> groupFallbackLines(const document::PageText& text, std::vector<int> line_indices) {
    std::stable_sort(line_indices.begin(), line_indices.end(), [&](int lhs, int rhs) {
        const document::BBox& left = text.lines[static_cast<std::size_t>(lhs)].bbox;
        const document::BBox& right = text.lines[static_cast<std::size_t>(rhs)].bbox;
        if (std::abs(left.y0 - right.y0) > 1.0e-3) {
            return left.y0 < right.y0;
        }
        return left.x0 < right.x0;
    });

    std::vector<FallbackLineGroup> groups;
    for (const int line_index : line_indices) {
        const document::BBox& bbox = text.lines[static_cast<std::size_t>(line_index)].bbox;
        int best_group = -1;
        double best_distance = std::numeric_limits<double>::max();
        for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
            const FallbackLineGroup& group = groups[group_index];
            if (!canJoinFallbackGroup(group.last_line_bbox, bbox)) {
                continue;
            }
            const double distance =
                std::max(0.0, bbox.y0 - group.last_line_bbox.y1) + std::abs(bbox.x0 - group.last_line_bbox.x0) * 0.10;
            if (distance < best_distance) {
                best_group = static_cast<int>(group_index);
                best_distance = distance;
            }
        }

        if (best_group < 0) {
            groups.push_back({bbox, bbox, {line_index}, {}});
            continue;
        }
        FallbackLineGroup& group = groups[static_cast<std::size_t>(best_group)];
        expandBBox(group.bbox, bbox);
        group.last_line_bbox = bbox;
        group.line_indices.push_back(line_index);
    }

    for (FallbackLineGroup& group : groups) {
        std::stable_sort(group.line_indices.begin(), group.line_indices.end(), [&](int lhs, int rhs) {
            const document::BBox& left = text.lines[static_cast<std::size_t>(lhs)].bbox;
            const document::BBox& right = text.lines[static_cast<std::size_t>(rhs)].bbox;
            const double overlap_y = std::max(0.0, std::min(left.y1, right.y1) - std::max(left.y0, right.y0));
            const double left_height = std::max(0.0, left.y1 - left.y0);
            const double right_height = std::max(0.0, right.y1 - right.y0);
            const double shorter_height = std::max(1.0e-3, std::min(left_height, right_height));
            if (overlap_y / shorter_height >= 0.50) {
                return left.x0 < right.x0;
            }
            if (std::abs(left.y0 - right.y0) > 1.0e-3) {
                return left.y0 < right.y0;
            }
            return left.x0 < right.x0;
        });
    }
    return groups;
}

bool parseDecimalMarker(const std::string& text, int& value) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return false;
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n") + 1;
    if (end - begin > 4 || !std::all_of(text.begin() + static_cast<std::ptrdiff_t>(begin),
                                        text.begin() + static_cast<std::ptrdiff_t>(end),
                                        [](unsigned char character) { return std::isdigit(character); })) {
        return false;
    }
    const char* first = text.data() + begin;
    const char* last = text.data() + end;
    const std::from_chars_result parsed = std::from_chars(first, last, value);
    return parsed.ec == std::errc{} && parsed.ptr == last;
}

struct DecimalMarker {
    int line_index = -1;
    int value = 0;
    double center_x = 0.0;
    double center_y = 0.0;
};

std::set<int> detectRepeatedMarginalia(const document::PageText& text,
                                       const document::PageArtifact& page,
                                       const std::vector<int>& line_indices,
                                       double median_line_height) {
    std::vector<DecimalMarker> markers;
    for (const int line_index : line_indices) {
        const document::TextLine& line = text.lines[static_cast<std::size_t>(line_index)];
        int value = 0;
        const double width = line.bbox.x1 - line.bbox.x0;
        const double maximum_width =
            std::max(static_cast<double>(std::max(1, page.width)) * 0.05, median_line_height * 5.0);
        if (!parseDecimalMarker(line.text, value) || width > maximum_width) {
            continue;
        }
        markers.push_back({
            line_index,
            value,
            (line.bbox.x0 + line.bbox.x1) * 0.5,
            (line.bbox.y0 + line.bbox.y1) * 0.5,
        });
    }

    std::stable_sort(markers.begin(), markers.end(), [](const DecimalMarker& lhs, const DecimalMarker& rhs) {
        return lhs.center_x < rhs.center_x;
    });
    const double x_tolerance =
        std::max({4.0, median_line_height * 1.25, static_cast<double>(std::max(1, page.width)) * 0.008});
    std::vector<std::vector<DecimalMarker>> clusters;
    for (const DecimalMarker& marker : markers) {
        int best_cluster = -1;
        double best_distance = std::numeric_limits<double>::max();
        for (std::size_t cluster_index = 0; cluster_index < clusters.size(); ++cluster_index) {
            double mean_x = 0.0;
            for (const DecimalMarker& member : clusters[cluster_index]) {
                mean_x += member.center_x;
            }
            mean_x /= static_cast<double>(clusters[cluster_index].size());
            const double distance = std::abs(marker.center_x - mean_x);
            if (distance <= x_tolerance && distance < best_distance) {
                best_cluster = static_cast<int>(cluster_index);
                best_distance = distance;
            }
        }
        if (best_cluster < 0) {
            clusters.push_back({marker});
        } else {
            clusters[static_cast<std::size_t>(best_cluster)].push_back(marker);
        }
    }

    std::set<int> marginalia;
    for (std::vector<DecimalMarker>& cluster : clusters) {
        if (cluster.size() < 4) {
            continue;
        }
        std::stable_sort(cluster.begin(), cluster.end(), [](const DecimalMarker& lhs, const DecimalMarker& rhs) {
            return lhs.center_y < rhs.center_y;
        });
        const double required_span =
            std::max(static_cast<double>(std::max(1, page.height)) * 0.25, median_line_height * 12.0);
        if (cluster.back().center_y - cluster.front().center_y < required_span) {
            continue;
        }

        std::vector<double> directional_steps;
        directional_steps.reserve(cluster.size() - 1);
        int positive_steps = 0;
        int negative_steps = 0;
        for (std::size_t index = 1; index < cluster.size(); ++index) {
            const int difference = cluster[index].value - cluster[index - 1].value;
            positive_steps += difference > 0 ? 1 : 0;
            negative_steps += difference < 0 ? 1 : 0;
        }
        const int direction = positive_steps >= negative_steps ? 1 : -1;
        for (std::size_t index = 1; index < cluster.size(); ++index) {
            const int difference = (cluster[index].value - cluster[index - 1].value) * direction;
            if (difference > 0) {
                directional_steps.push_back(static_cast<double>(difference));
            }
        }
        const int transition_count = static_cast<int>(cluster.size() - 1);
        const int required_monotonic = static_cast<int>(std::ceil(transition_count * 0.80));
        if (static_cast<int>(directional_steps.size()) < required_monotonic) {
            continue;
        }
        const double typical_step = median(directional_steps);
        const int consistent_steps =
            static_cast<int>(std::count_if(directional_steps.begin(), directional_steps.end(), [&](double step) {
                return step >= typical_step * 0.45 && step <= typical_step * 2.25;
            }));
        const int required_consistent = static_cast<int>(std::ceil(transition_count * 0.75));
        if (typical_step <= 0.0 || consistent_steps < required_consistent) {
            continue;
        }
        for (const DecimalMarker& marker : cluster) {
            marginalia.insert(marker.line_index);
        }
    }
    return marginalia;
}

double groupCenterX(const FallbackLineGroup& group) { return (group.bbox.x0 + group.bbox.x1) * 0.5; }

double groupCenterY(const FallbackLineGroup& group) { return (group.bbox.y0 + group.bbox.y1) * 0.5; }

template <typename Coordinate>
int alignedClusterCount(const std::vector<FallbackLineGroup>& groups,
                        const std::vector<std::size_t>& indices,
                        Coordinate coordinate,
                        double tolerance) {
    std::vector<double> values;
    values.reserve(indices.size());
    for (const std::size_t index : indices) {
        values.push_back(coordinate(groups[index]));
    }
    std::sort(values.begin(), values.end());

    int qualifying_clusters = 0;
    for (std::size_t begin = 0; begin < values.size();) {
        double mean = values[begin];
        std::size_t end = begin + 1;
        while (end < values.size() && std::abs(values[end] - mean) <= tolerance) {
            mean = (mean * static_cast<double>(end - begin) + values[end]) / static_cast<double>(end - begin + 1);
            ++end;
        }
        qualifying_clusters += end - begin >= 2 ? 1 : 0;
        begin = end;
    }
    return qualifying_clusters;
}

std::vector<FallbackLineGroup> coalesceDenseGridGroups(const document::PageText& text,
                                                       const document::PageArtifact& page,
                                                       std::vector<FallbackLineGroup> groups,
                                                       double median_line_height,
                                                       LayoutRecoveryStats& stats) {
    if (groups.size() < 6 || median_line_height <= 0.0) {
        return groups;
    }

    const double page_width = static_cast<double>(std::max(1, page.width));
    const double page_height = static_cast<double>(std::max(1, page.height));
    const double row_tolerance = std::max(median_line_height * 1.50, page_height * 0.012);
    const double column_tolerance = std::max(median_line_height * 2.50, page_width * 0.025);

    while (true) {
        std::vector<std::size_t> candidates;
        for (std::size_t index = 0; index < groups.size(); ++index) {
            const FallbackLineGroup& group = groups[index];
            const double width = group.bbox.x1 - group.bbox.x0;
            const double height = group.bbox.y1 - group.bbox.y0;
            if (group.source_label.empty() && group.line_indices.size() <= 3 && width <= page_width * 0.45 &&
                height <= std::max(median_line_height * 3.25, page_height * 0.06)) {
                candidates.push_back(index);
            }
        }
        if (candidates.size() < 6) {
            break;
        }

        std::vector<bool> visited(candidates.size(), false);
        std::vector<std::size_t> selected;
        for (std::size_t start = 0; start < candidates.size() && selected.empty(); ++start) {
            if (visited[start]) {
                continue;
            }
            std::vector<std::size_t> component_positions{start};
            visited[start] = true;
            for (std::size_t cursor = 0; cursor < component_positions.size(); ++cursor) {
                const std::size_t lhs_position = component_positions[cursor];
                const FallbackLineGroup& lhs = groups[candidates[lhs_position]];
                for (std::size_t rhs_position = 0; rhs_position < candidates.size(); ++rhs_position) {
                    if (visited[rhs_position]) {
                        continue;
                    }
                    const FallbackLineGroup& rhs = groups[candidates[rhs_position]];
                    const bool same_row = std::abs(groupCenterY(lhs) - groupCenterY(rhs)) <= row_tolerance;
                    const bool same_column = std::abs(lhs.bbox.x0 - rhs.bbox.x0) <= column_tolerance;
                    const bool row_neighbor = same_row &&
                                              intervalGap(lhs.bbox.x0, lhs.bbox.x1, rhs.bbox.x0, rhs.bbox.x1) <=
                                                  page_width * 0.35;
                    const bool column_neighbor = same_column &&
                                                 intervalGap(lhs.bbox.y0, lhs.bbox.y1, rhs.bbox.y0, rhs.bbox.y1) <=
                                                     std::max(median_line_height * 8.0, page_height * 0.18);
                    if (row_neighbor || column_neighbor) {
                        visited[rhs_position] = true;
                        component_positions.push_back(rhs_position);
                    }
                }
            }

            std::vector<std::size_t> component;
            component.reserve(component_positions.size());
            for (const std::size_t position : component_positions) {
                component.push_back(candidates[position]);
            }
            if (component.size() >= 6 && alignedClusterCount(groups, component, groupCenterY, row_tolerance) >= 2 &&
                alignedClusterCount(
                    groups, component, [](const FallbackLineGroup& group) { return group.bbox.x0; }, column_tolerance) >=
                    2) {
                selected = std::move(component);
            }
        }
        if (selected.empty()) {
            break;
        }

        document::BBox grid_bbox = groups[selected.front()].bbox;
        for (const std::size_t index : selected) {
            expandBBox(grid_bbox, groups[index].bbox);
        }
        std::set<std::size_t> merge_indices(selected.begin(), selected.end());
        for (std::size_t index = 0; index < groups.size(); ++index) {
            if (merge_indices.find(index) != merge_indices.end() || !groups[index].source_label.empty() ||
                groups[index].line_indices.size() > 4) {
                continue;
            }
            const FallbackLineGroup& group = groups[index];
            const double height = group.bbox.y1 - group.bbox.y0;
            const double center_x = groupCenterX(group);
            const double center_y = groupCenterY(group);
            if (height <= median_line_height * 6.0 && center_x >= grid_bbox.x0 - median_line_height * 2.0 &&
                center_x <= grid_bbox.x1 + median_line_height * 2.0 &&
                center_y >= grid_bbox.y0 - median_line_height * 2.0 &&
                center_y <= grid_bbox.y1 + median_line_height * 2.0) {
                merge_indices.insert(index);
            }
        }

        FallbackLineGroup merged = groups[*merge_indices.begin()];
        merged.source_label = "text_grid_fallback";
        merged.line_indices.clear();
        for (const std::size_t index : merge_indices) {
            expandBBox(merged.bbox, groups[index].bbox);
            merged.line_indices.insert(
                merged.line_indices.end(), groups[index].line_indices.begin(), groups[index].line_indices.end());
        }
        sortLineIndicesRowWise(text, merged.line_indices, median_line_height);
        merged.last_line_bbox = text.lines[static_cast<std::size_t>(merged.line_indices.back())].bbox;

        std::vector<FallbackLineGroup> reduced;
        reduced.reserve(groups.size() - merge_indices.size() + 1);
        for (std::size_t index = 0; index < groups.size(); ++index) {
            if (index == *merge_indices.begin()) {
                reduced.push_back(std::move(merged));
            }
            if (merge_indices.find(index) == merge_indices.end()) {
                reduced.push_back(std::move(groups[index]));
            }
        }
        stats.coalesced_grid_groups += static_cast<int>(merge_indices.size());
        groups = std::move(reduced);
    }
    return groups;
}

document::LayoutBlockType fallbackBlockType(const document::BBox& bbox, const document::PageArtifact& page) {
    if (page.width <= 0 || page.height <= 0) {
        return document::LayoutBlockType::Text;
    }

    const double center_y = (bbox.y0 + bbox.y1) * 0.5 / static_cast<double>(page.height);
    if (center_y <= 0.05) {
        return document::LayoutBlockType::Header;
    }
    if (center_y >= 0.95) {
        return document::LayoutBlockType::Footer;
    }
    return document::LayoutBlockType::Text;
}

bool supportsFurnitureRefinement(document::LayoutBlockType type) {
    return type == document::LayoutBlockType::Unknown || type == document::LayoutBlockType::Text ||
           type == document::LayoutBlockType::List;
}

const char* fallbackSourceLabel(document::LayoutBlockType type) {
    switch (type) {
    case document::LayoutBlockType::Header:
        return "edge_header_fallback";
    case document::LayoutBlockType::Footer:
        return "edge_footer_fallback";
    default:
        return "text_line_fallback";
    }
}

double medianTextLineHeight(const document::PageText& text) {
    std::vector<double> heights;
    heights.reserve(text.lines.size());
    for (const document::TextLine& line : text.lines) {
        const double height = line.bbox.y1 - line.bbox.y0;
        if (height > 0.0 && hasVisibleText(line.text)) {
            heights.push_back(height);
        }
    }
    return median(std::move(heights));
}

std::string fallbackGroupText(const document::PageText& text, const FallbackLineGroup& group) {
    std::string result;
    for (const int line_index : group.line_indices) {
        if (line_index < 0 || static_cast<std::size_t>(line_index) >= text.lines.size()) {
            continue;
        }
        if (!result.empty()) {
            result += '\n';
        }
        result += text.lines[static_cast<std::size_t>(line_index)].text;
    }
    return result;
}

std::string asciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool isCompactPageMarker(const std::string& text) {
    int digits = 0;
    int letters = 0;
    int roman_letters = 0;
    int visible = 0;
    for (const unsigned char character : text) {
        if (std::isspace(character)) {
            continue;
        }
        ++visible;
        if (std::isdigit(character)) {
            ++digits;
        } else if (std::isalpha(character)) {
            ++letters;
            const unsigned char lower = static_cast<unsigned char>(std::tolower(character));
            if (lower == 'i' || lower == 'v' || lower == 'x' || lower == 'l' || lower == 'c' || lower == 'd' ||
                lower == 'm') {
                ++roman_letters;
            }
        } else if (character != '-' && character != '/' && character != '|' && character != '(' && character != ')' &&
                   character != '.' && character != ':') {
            return false;
        }
    }
    if (visible == 0 || visible > 16) {
        return false;
    }
    return (digits > 0 && letters <= 4) || (digits == 0 && letters >= 1 && letters == roman_letters);
}

bool hasStrongFurniturePattern(const std::string& text) {
    const std::string lower = asciiLower(text);
    constexpr const char* patterns[] = {
        "copyright",
        "all rights reserved",
        "doi:",
        "http://",
        "https://",
        "www.",
        "issn",
        "journal of",
        "downloaded",
        "document downloaded",
    };
    if (std::any_of(std::begin(patterns), std::end(patterns), [&](const char* pattern) {
            return lower.find(pattern) != std::string::npos;
        })) {
        return true;
    }
    return isCompactPageMarker(lower);
}

bool startsWithAny(const std::string& value, const std::vector<std::string>& prefixes) {
    const std::size_t first_visible = value.find_first_not_of(" \t\r\n-");
    if (first_visible == std::string::npos) {
        return false;
    }
    return std::any_of(prefixes.begin(), prefixes.end(), [&](const std::string& prefix) {
        return value.compare(first_visible, prefix.size(), prefix) == 0;
    });
}

bool hasBodyCaptionPrefix(const std::string& text) {
    const std::string lower = asciiLower(text);
    static const std::vector<std::string> prefixes = {
        "fig.",
        "figure ",
        "table ",
        "above:",
        "below:",
        "left:",
        "right:",
        "photo:",
        "source:",
    };
    return startsWithAny(lower, prefixes);
}

int visibleCharacterCount(const std::string& text) {
    return static_cast<int>(
        std::count_if(text.begin(), text.end(), [](unsigned char character) { return !std::isspace(character); }));
}

bool hasBodyParagraphShape(const FallbackLineGroup& group, const std::string& text) {
    return group.line_indices.size() >= 4 && visibleCharacterCount(text) >= 100;
}

bool supportsRecoveryAttachment(document::LayoutBlockType type) {
    return type == document::LayoutBlockType::Unknown || type == document::LayoutBlockType::Title ||
           type == document::LayoutBlockType::Text || type == document::LayoutBlockType::List;
}

bool hasLeadingListMarker(const std::string& text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return false;
    }
    if (text[begin] == '-' || text[begin] == '*' || text[begin] == '+') {
        return true;
    }
    std::size_t cursor = begin;
    while (cursor < text.size() && std::isdigit(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    return cursor > begin && cursor < text.size() &&
           (text[cursor] == '.' || text[cursor] == ')' || text[cursor] == ':' || text[cursor] == '-');
}

bool nearNonTextRegion(const FallbackLineGroup& group,
                       const std::vector<document::LayoutBlock>& blocks,
                       double median_line_height) {
    return std::any_of(blocks.begin(), blocks.end(), [&](const document::LayoutBlock& block) {
        if (block.type != document::LayoutBlockType::Figure && block.type != document::LayoutBlockType::Table) {
            return false;
        }
        const double group_width = group.bbox.x1 - group.bbox.x0;
        const double block_width = block.bbox.x1 - block.bbox.x0;
        const double overlap_ratio =
            horizontalOverlap(group.bbox, block.bbox) / std::max(1.0e-3, std::min(group_width, block_width));
        const double gap = intervalGap(group.bbox.y0, group.bbox.y1, block.bbox.y0, block.bbox.y1);
        return overlap_ratio >= 0.25 && gap <= median_line_height * 2.0;
    });
}

int attachmentTarget(const document::PageText& text,
                     const FallbackLineGroup& group,
                     const std::vector<document::LayoutBlock>& blocks,
                     const document::PageArtifact& page,
                     double median_line_height) {
    if (!group.source_label.empty() || median_line_height <= 0.0 || group.line_indices.size() > 2 ||
        group.bbox.y1 - group.bbox.y0 > median_line_height * 2.25) {
        return -1;
    }
    const std::string group_text = fallbackGroupText(text, group);
    if (isCompactPageMarker(group_text) || hasBodyCaptionPrefix(group_text) || visibleCharacterCount(group_text) < 4 ||
        nearNonTextRegion(group, blocks, median_line_height)) {
        return -1;
    }
    const bool list_item = hasLeadingListMarker(group_text);
    const double group_width = group.bbox.x1 - group.bbox.x0;
    const double page_width = static_cast<double>(std::max(1, page.width));
    if (group_width < std::max(median_line_height * 5.0, page_width * 0.06)) {
        return -1;
    }

    int best_target = -1;
    double best_score = std::numeric_limits<double>::max();
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        const document::LayoutBlock& block = blocks[index];
        if (!supportsRecoveryAttachment(block.type) || block.text_line_indices.empty() || !validBBox(block.bbox)) {
            continue;
        }
        if (list_item && block.type == document::LayoutBlockType::Title) {
            continue;
        }
        const double block_width = block.bbox.x1 - block.bbox.x0;
        const double overlap_ratio =
            horizontalOverlap(group.bbox, block.bbox) / std::max(1.0e-3, std::min(group_width, block_width));
        if (overlap_ratio < 0.60) {
            continue;
        }
        const double vertical_gap = intervalGap(group.bbox.y0, group.bbox.y1, block.bbox.y0, block.bbox.y1);
        if (vertical_gap > median_line_height * 1.25) {
            continue;
        }
        const double score = vertical_gap + std::abs(group.bbox.x0 - block.bbox.x0) * 0.05;
        if (score < best_score) {
            best_target = static_cast<int>(index);
            best_score = score;
        }
    }
    return best_target;
}

bool hasShortUppercaseKicker(const document::PageText& text, const FallbackLineGroup& group) {
    if (group.line_indices.size() < 2 || group.line_indices.size() > 3) {
        return false;
    }
    const int first_line_index = group.line_indices.front();
    if (first_line_index < 0 || static_cast<std::size_t>(first_line_index) >= text.lines.size()) {
        return false;
    }

    int uppercase_letters = 0;
    int lowercase_letters = 0;
    int visible = 0;
    for (const unsigned char character : text.lines[static_cast<std::size_t>(first_line_index)].text) {
        if (std::isspace(character)) {
            continue;
        }
        ++visible;
        if (character >= 'A' && character <= 'Z') {
            ++uppercase_letters;
        } else if (character >= 'a' && character <= 'z') {
            ++lowercase_letters;
        }
    }
    if (visible > 32 || uppercase_letters < 3 || lowercase_letters != 0) {
        return false;
    }

    const int second_line_index = group.line_indices[1];
    if (second_line_index < 0 || static_cast<std::size_t>(second_line_index) >= text.lines.size()) {
        return false;
    }
    const std::string& second_line = text.lines[static_cast<std::size_t>(second_line_index)].text;
    return std::count_if(second_line.begin(), second_line.end(), [](unsigned char character) {
               return character >= 'a' && character <= 'z';
           }) >= 3;
}

bool supportsBodyContinuity(document::LayoutBlockType type) {
    return type != document::LayoutBlockType::Header && type != document::LayoutBlockType::Footer &&
           type != document::LayoutBlockType::Figure;
}

bool continuesModelBackedBody(const FallbackLineGroup& group,
                              const std::vector<document::LayoutBlock>& blocks,
                              double median_line_height) {
    if (median_line_height <= 0.0) {
        return false;
    }
    const double group_width = group.bbox.x1 - group.bbox.x0;
    if (group_width <= 0.0) {
        return false;
    }

    for (const document::LayoutBlock& block : blocks) {
        if (!supportsBodyContinuity(block.type) || block.text_line_indices.empty() || !validBBox(block.bbox)) {
            continue;
        }
        const double block_width = block.bbox.x1 - block.bbox.x0;
        const double width_ratio = group_width / std::max(1.0e-3, block_width);
        if (width_ratio > 1.25) {
            continue;
        }
        const double overlap_ratio =
            horizontalOverlap(group.bbox, block.bbox) / std::max(1.0e-3, std::min(group_width, block_width));
        if (overlap_ratio < 0.50) {
            continue;
        }
        const double vertical_gap = intervalGap(group.bbox.y0, group.bbox.y1, block.bbox.y0, block.bbox.y1);
        if (vertical_gap <= median_line_height) {
            return true;
        }
    }
    return false;
}

bool isLargeTopHeading(const document::PageText& text, const FallbackLineGroup& group, double median_line_height) {
    if (median_line_height <= 0.0 || group.line_indices.empty() || group.line_indices.size() > 2) {
        return false;
    }
    std::vector<double> heights;
    heights.reserve(group.line_indices.size());
    for (const int line_index : group.line_indices) {
        if (line_index < 0 || static_cast<std::size_t>(line_index) >= text.lines.size()) {
            return false;
        }
        const document::TextLine& line = text.lines[static_cast<std::size_t>(line_index)];
        heights.push_back(line.bbox.y1 - line.bbox.y0);
    }
    const std::string text_value = fallbackGroupText(text, group);
    const int visible_characters = static_cast<int>(std::count_if(
        text_value.begin(), text_value.end(), [](unsigned char character) { return !std::isspace(character); }));
    return visible_characters >= 8 && median(std::move(heights)) >= median_line_height * 1.75;
}

document::LayoutBlockType recoveredGroupType(const document::PageText& text,
                                             const document::PageArtifact& page,
                                             const std::vector<document::LayoutBlock>& blocks,
                                             const FallbackLineGroup& group,
                                             double median_line_height) {
    if (page.height <= 0) {
        return document::LayoutBlockType::Text;
    }
    const double center_y = (group.bbox.y0 + group.bbox.y1) * 0.5 / static_cast<double>(page.height);
    const bool header_candidate = center_y <= 0.15;
    const bool footer_candidate = center_y >= 0.88;
    if (!header_candidate && !footer_candidate) {
        return document::LayoutBlockType::Text;
    }
    const bool strict_page_edge = center_y <= 0.05 || center_y >= 0.95;

    const std::string text_value = fallbackGroupText(text, group);
    const bool strong_furniture = hasStrongFurniturePattern(text_value);
    const bool body_continuation = continuesModelBackedBody(group, blocks, median_line_height);
    if (hasBodyCaptionPrefix(text_value)) {
        return document::LayoutBlockType::Text;
    }
    if (!strong_furniture && hasBodyParagraphShape(group, text_value)) {
        return document::LayoutBlockType::Text;
    }
    if (!strong_furniture && body_continuation) {
        return document::LayoutBlockType::Text;
    }
    if (header_candidate && !strict_page_edge && !strong_furniture &&
        (isLargeTopHeading(text, group, median_line_height) || hasShortUppercaseKicker(text, group))) {
        return document::LayoutBlockType::Text;
    }
    return header_candidate ? document::LayoutBlockType::Header : document::LayoutBlockType::Footer;
}

std::vector<LineColumn>
detectLineColumns(const document::PageText& text, const document::LayoutBlock& block, double median_height) {
    std::vector<double> widths;
    widths.reserve(block.text_line_indices.size());
    for (const int line_index : block.text_line_indices) {
        if (line_index < 0 || static_cast<std::size_t>(line_index) >= text.lines.size()) {
            return {};
        }
        widths.push_back(std::max(0.0,
                                  text.lines[static_cast<std::size_t>(line_index)].bbox.x1 -
                                      text.lines[static_cast<std::size_t>(line_index)].bbox.x0));
    }
    const double median_width = median(widths);
    if (median_width <= 0.0) {
        return {};
    }

    std::vector<int> ordered = block.text_line_indices;
    std::sort(ordered.begin(), ordered.end(), [&](int lhs, int rhs) {
        const document::BBox& left = text.lines[static_cast<std::size_t>(lhs)].bbox;
        const document::BBox& right = text.lines[static_cast<std::size_t>(rhs)].bbox;
        const double left_center = (left.x0 + left.x1) * 0.5;
        const double right_center = (right.x0 + right.x1) * 0.5;
        if (std::abs(left_center - right_center) > 1.0e-3) {
            return left_center < right_center;
        }
        return left.y0 < right.y0;
    });

    std::vector<LineColumn> columns;
    for (const int line_index : ordered) {
        const document::BBox& bbox = text.lines[static_cast<std::size_t>(line_index)].bbox;
        const double width = std::max(0.0, bbox.x1 - bbox.x0);
        if (width > median_width * 1.35 + 1.0e-3) {
            return {};
        }

        int best_column = -1;
        double best_ratio = 0.0;
        for (std::size_t column_index = 0; column_index < columns.size(); ++column_index) {
            const double overlap = horizontalOverlap(bbox, columns[column_index].bbox);
            const double column_width = columns[column_index].bbox.x1 - columns[column_index].bbox.x0;
            const double ratio = overlap / std::max(1.0e-3, std::min(width, column_width));
            if (ratio >= 0.50 && ratio > best_ratio) {
                best_column = static_cast<int>(column_index);
                best_ratio = ratio;
            }
        }

        if (best_column < 0) {
            columns.push_back({bbox, {line_index}});
        } else {
            LineColumn& column = columns[static_cast<std::size_t>(best_column)];
            column.bbox.x0 = std::min(column.bbox.x0, bbox.x0);
            column.bbox.y0 = std::min(column.bbox.y0, bbox.y0);
            column.bbox.x1 = std::max(column.bbox.x1, bbox.x1);
            column.bbox.y1 = std::max(column.bbox.y1, bbox.y1);
            column.line_indices.push_back(line_index);
        }
    }

    std::sort(columns.begin(), columns.end(), [](const LineColumn& lhs, const LineColumn& rhs) {
        return lhs.bbox.x0 < rhs.bbox.x0;
    });
    if (columns.size() < 2 || columns.size() > 4) {
        return {};
    }

    for (std::size_t index = 0; index < columns.size(); ++index) {
        if (columns[index].line_indices.size() < 2) {
            return {};
        }
        if (index + 1 >= columns.size()) {
            continue;
        }
        const LineColumn& left = columns[index];
        const LineColumn& right = columns[index + 1];
        const double gutter = right.bbox.x0 - left.bbox.x1;
        if (gutter <= median_height * 0.75) {
            return {};
        }
        const double vertical_overlap =
            std::max(0.0, std::min(left.bbox.y1, right.bbox.y1) - std::max(left.bbox.y0, right.bbox.y0));
        const double shorter_span =
            std::max(1.0e-3, std::min(left.bbox.y1 - left.bbox.y0, right.bbox.y1 - right.bbox.y0));
        if (vertical_overlap / shorter_span < 0.25) {
            return {};
        }
    }

    return columns;
}

} // namespace

void assignTextLines(const document::PageText& text, std::vector<document::LayoutBlock>& blocks) {
    for (std::size_t line_index = 0; line_index < text.lines.size(); ++line_index) {
        const document::BBox& line_bbox = text.lines[line_index].bbox;
        int best_index = -1;
        double best_coverage = 0.0;
        double best_area = std::numeric_limits<double>::max();
        for (std::size_t block_index = 0; block_index < blocks.size(); ++block_index) {
            const double coverage = overlapOverLine(line_bbox, blocks[block_index].bbox);
            const bool center_inside = containsCenter(blocks[block_index].bbox, line_bbox);
            if (coverage < 0.5 && !center_inside) {
                continue;
            }
            const double area = bboxArea(blocks[block_index].bbox);
            if (coverage > best_coverage || (std::abs(coverage - best_coverage) < 1.0e-9 && area < best_area)) {
                best_index = static_cast<int>(block_index);
                best_coverage = coverage;
                best_area = area;
            }
        }
        if (best_index >= 0) {
            blocks[static_cast<std::size_t>(best_index)].text_line_indices.push_back(static_cast<int>(line_index));
        }
    }
}

void associateCaptions(const document::PageArtifact& page, std::vector<document::LayoutBlock>& blocks) {
    const double page_diagonal =
        std::hypot(static_cast<double>(std::max(1, page.width)), static_cast<double>(std::max(1, page.height)));
    const double maximum_distance = page_diagonal * 0.25;
    for (document::LayoutBlock& caption : blocks) {
        if (!isCaptionLabel(caption.source_label)) {
            continue;
        }

        const document::LayoutBlock* best_target = nullptr;
        double best_distance = std::numeric_limits<double>::max();
        for (const document::LayoutBlock& candidate : blocks) {
            if (candidate.type != document::LayoutBlockType::Figure &&
                candidate.type != document::LayoutBlockType::Table) {
                continue;
            }
            const double distance = captionDistance(caption.bbox, candidate.bbox);
            if (distance < best_distance) {
                best_distance = distance;
                best_target = &candidate;
            }
        }
        if (best_target != nullptr && best_distance <= maximum_distance) {
            caption.related_block_id = best_target->id;
        }
    }
}

LayoutRefinementStats refineMultiColumnTextLineOrder(const document::PageText& text,
                                                     std::vector<document::LayoutBlock>& blocks) {
    LayoutRefinementStats stats;
    for (document::LayoutBlock& block : blocks) {
        if (!supportsLineRefinement(block.type) || block.source_label == "text_grid_fallback" ||
            block.text_line_indices.size() < 4) {
            continue;
        }

        std::vector<double> heights;
        heights.reserve(block.text_line_indices.size());
        for (const int line_index : block.text_line_indices) {
            if (line_index < 0 || static_cast<std::size_t>(line_index) >= text.lines.size()) {
                heights.clear();
                break;
            }
            const document::BBox& bbox = text.lines[static_cast<std::size_t>(line_index)].bbox;
            heights.push_back(std::max(0.0, bbox.y1 - bbox.y0));
        }
        const double median_height = median(std::move(heights));
        if (median_height <= 0.0) {
            continue;
        }

        std::vector<LineColumn> columns = detectLineColumns(text, block, median_height);
        if (columns.empty()) {
            continue;
        }

        std::vector<int> refined;
        refined.reserve(block.text_line_indices.size());
        for (LineColumn& column : columns) {
            std::sort(column.line_indices.begin(), column.line_indices.end(), [&](int lhs, int rhs) {
                const document::BBox& left = text.lines[static_cast<std::size_t>(lhs)].bbox;
                const document::BBox& right = text.lines[static_cast<std::size_t>(rhs)].bbox;
                if (std::abs(left.y0 - right.y0) > 1.0e-3) {
                    return left.y0 < right.y0;
                }
                return left.x0 < right.x0;
            });
            refined.insert(refined.end(), column.line_indices.begin(), column.line_indices.end());
        }

        if (refined != block.text_line_indices) {
            block.text_line_indices = std::move(refined);
            ++stats.reordered_blocks;
            stats.maximum_columns = std::max(stats.maximum_columns, static_cast<int>(columns.size()));
        }
    }
    return stats;
}

EdgeFurnitureRefinementStats refineEdgeFurniture(const document::PageArtifact& page,
                                                 std::vector<document::LayoutBlock>& blocks) {
    EdgeFurnitureRefinementStats stats;
    if (page.width <= 0 || page.height <= 0) {
        return stats;
    }

    for (document::LayoutBlock& block : blocks) {
        if (!supportsFurnitureRefinement(block.type) || !validBBox(block.bbox)) {
            continue;
        }
        const document::LayoutBlockType refined = fallbackBlockType(block.bbox, page);
        if (refined == document::LayoutBlockType::Header) {
            block.type = refined;
            block.confidence = std::min(block.confidence, 0.50);
            ++stats.headers;
        } else if (refined == document::LayoutBlockType::Footer) {
            block.type = refined;
            block.confidence = std::min(block.confidence, 0.50);
            ++stats.footers;
        }
    }
    return stats;
}

LayoutRecoveryStats recoverUnassignedTextLines(const document::PageText& text,
                                               const document::PageArtifact& page,
                                               document::PageLayout& layout) {
    LayoutRecoveryStats stats;
    const double median_line_height = medianTextLineHeight(text);
    std::set<int> assigned;
    for (const document::LayoutBlock& block : layout.blocks) {
        for (const int line_index : block.text_line_indices) {
            if (line_index >= 0 && static_cast<std::size_t>(line_index) < text.lines.size()) {
                assigned.insert(line_index);
            }
        }
    }

    std::vector<int> recoverable;
    for (std::size_t line_index = 0; line_index < text.lines.size(); ++line_index) {
        if (assigned.find(static_cast<int>(line_index)) != assigned.end()) {
            continue;
        }
        const document::TextLine& line = text.lines[line_index];
        if (!hasVisibleText(line.text) || !validBBox(line.bbox)) {
            continue;
        }
        if (overlapsFurniture(line.bbox, layout.blocks)) {
            ++stats.skipped_furniture_lines;
            continue;
        }
        recoverable.push_back(static_cast<int>(line_index));
    }

    const std::set<int> marginalia = detectRepeatedMarginalia(text, page, recoverable, median_line_height);
    recoverable.erase(std::remove_if(recoverable.begin(),
                                     recoverable.end(),
                                     [&](int line_index) { return marginalia.find(line_index) != marginalia.end(); }),
                      recoverable.end());
    stats.skipped_marginalia_lines = static_cast<int>(marginalia.size());

    std::vector<FallbackLineGroup> body_groups;
    std::vector<FallbackLineGroup> header_groups;
    std::vector<FallbackLineGroup> footer_groups;
    std::vector<FallbackLineGroup> fallback_groups = coalesceDenseGridGroups(
        text, page, groupFallbackLines(text, std::move(recoverable)), median_line_height, stats);
    for (FallbackLineGroup& group : fallback_groups) {
        switch (recoveredGroupType(text, page, layout.blocks, group, median_line_height)) {
        case document::LayoutBlockType::Header:
            header_groups.push_back(std::move(group));
            break;
        case document::LayoutBlockType::Footer:
            footer_groups.push_back(std::move(group));
            break;
        default:
            body_groups.push_back(std::move(group));
            break;
        }
    }

    const auto append_groups = [&](std::vector<FallbackLineGroup> groups, document::LayoutBlockType type) {
        for (FallbackLineGroup& group : groups) {
            document::LayoutBlock block;
            const int first_line_index = *std::min_element(group.line_indices.begin(), group.line_indices.end());
            block.id =
                "page_" + std::to_string(layout.page_number) + "_fallback_line_" + std::to_string(first_line_index + 1);
            block.type = type;
            block.source_label = type == document::LayoutBlockType::Text && !group.source_label.empty()
                                     ? group.source_label
                                     : fallbackSourceLabel(type);
            block.bbox = group.bbox;
            block.confidence = type == document::LayoutBlockType::Text ? 0.35 : 0.25;
            block.text_line_indices = std::move(group.line_indices);
            layout.blocks.push_back(std::move(block));
            const int line_count = static_cast<int>(layout.blocks.back().text_line_indices.size());
            stats.recovered_lines += line_count;
            if (type == document::LayoutBlockType::Header || type == document::LayoutBlockType::Footer) {
                stats.recovered_furniture_lines += line_count;
            }
            ++stats.fallback_blocks;
        }
    };
    for (const FallbackLineGroup& group : body_groups) {
        const double center_y =
            page.height > 0 ? (group.bbox.y0 + group.bbox.y1) * 0.5 / static_cast<double>(page.height) : 0.5;
        if (center_y <= 0.15 || center_y >= 0.88) {
            stats.preserved_edge_body_lines += static_cast<int>(group.line_indices.size());
        }
    }

    std::vector<FallbackLineGroup> unattached_body_groups;
    unattached_body_groups.reserve(body_groups.size());
    for (FallbackLineGroup& group : body_groups) {
        const int target_index = attachmentTarget(text, group, layout.blocks, page, median_line_height);
        if (target_index < 0) {
            unattached_body_groups.push_back(std::move(group));
            continue;
        }
        document::LayoutBlock& target = layout.blocks[static_cast<std::size_t>(target_index)];
        const int attached_line_count = static_cast<int>(group.line_indices.size());
        target.text_line_indices.insert(
            target.text_line_indices.end(), group.line_indices.begin(), group.line_indices.end());
        sortLineIndicesRowWise(text, target.text_line_indices, median_line_height);
        expandBBox(target.bbox, group.bbox);
        stats.recovered_lines += attached_line_count;
        stats.attached_lines += attached_line_count;
        ++stats.attached_groups;
    }
    body_groups = std::move(unattached_body_groups);

    append_groups(std::move(body_groups), document::LayoutBlockType::Text);
    append_groups(std::move(header_groups), document::LayoutBlockType::Header);
    append_groups(std::move(footer_groups), document::LayoutBlockType::Footer);
    refineMultiColumnTextLineOrder(text, layout.blocks);
    return stats;
}

} // namespace doc_parser::layout::detail
