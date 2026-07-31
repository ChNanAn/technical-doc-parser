#include "reading_order/reading_order_backend.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace doc_parser::reading_order {
namespace {

using document::BBox;
using document::LayoutBlock;
using document::LayoutBlockType;

constexpr double kEpsilon = 1.0e-3;
constexpr double kRegularWidthScale = 1.35;
constexpr double kColumnCenterToleranceScale = 0.22;
constexpr double kMinimumGutterHeightScale = 0.10;
constexpr double kMinimumGutterWidthScale = 0.02;
constexpr double kSameColumnConfidence = 1.0;
constexpr double kSpanningGeometryConfidence = 0.95;
constexpr double kColumnFlowConfidence = 0.80;
constexpr double kModelHintConfidence = 0.25;
constexpr std::size_t kMaximumColumns = 6;

struct PageElement {
    int layout_block_index = -1;
    const LayoutBlock* block = nullptr;
    BBox bbox;
};

struct Column {
    double x0 = 0.0;
    double x1 = 0.0;
};

struct ColumnCluster {
    std::vector<int> members;
    double weighted_center_sum = 0.0;
    double vertical_support = 0.0;

    double center() const { return vertical_support > kEpsilon ? weighted_center_sum / vertical_support : 0.0; }
};

struct ColumnRange {
    int first = 0;
    int last = 0;

    bool spanning() const { return first != last; }
};

struct WeightedEdge {
    int from = -1;
    int to = -1;
    double confidence = 0.0;
    std::string reason;
    bool active = true;
};

struct BandOrder {
    std::vector<int> indices;
    std::vector<ColumnRange> ranges;
    int column_count = 1;
};

double bboxWidth(const BBox& bbox) { return std::max(0.0, bbox.x1 - bbox.x0); }

double bboxHeight(const BBox& bbox) { return std::max(0.0, bbox.y1 - bbox.y0); }

double centerX(const BBox& bbox) { return (bbox.x0 + bbox.x1) * 0.5; }

double centerY(const BBox& bbox) { return (bbox.y0 + bbox.y1) * 0.5; }

double horizontalOverlap(const BBox& bbox, const Column& column) {
    return std::max(0.0, std::min(bbox.x1, column.x1) - std::max(bbox.x0, column.x0));
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

double weightedMedian(std::vector<std::pair<double, double>> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    double total_weight = 0.0;
    for (const auto& [value, weight] : values) {
        (void)value;
        total_weight += weight;
    }
    double cumulative_weight = 0.0;
    for (const auto& [value, weight] : values) {
        cumulative_weight += weight;
        if (cumulative_weight * 2.0 >= total_weight) {
            return value;
        }
    }
    return values.back().first;
}

bool isPreferredColumnSeed(LayoutBlockType type) {
    return type == LayoutBlockType::Text || type == LayoutBlockType::List || type == LayoutBlockType::Unknown;
}

std::vector<int> sortedByPosition(const std::vector<PageElement>& elements, std::vector<int> indices) {
    std::sort(indices.begin(), indices.end(), [&](int lhs, int rhs) {
        const PageElement& left = elements[static_cast<std::size_t>(lhs)];
        const PageElement& right = elements[static_cast<std::size_t>(rhs)];
        if (std::abs(left.bbox.y0 - right.bbox.y0) > kEpsilon) {
            return left.bbox.y0 < right.bbox.y0;
        }
        if (std::abs(left.bbox.x0 - right.bbox.x0) > kEpsilon) {
            return left.bbox.x0 < right.bbox.x0;
        }
        return left.layout_block_index < right.layout_block_index;
    });
    return indices;
}

void mergeColumnPair(std::vector<Column>& columns, std::size_t left_index) {
    columns[left_index].x0 = std::min(columns[left_index].x0, columns[left_index + 1].x0);
    columns[left_index].x1 = std::max(columns[left_index].x1, columns[left_index + 1].x1);
    columns.erase(columns.begin() + static_cast<std::ptrdiff_t>(left_index + 1));
}

std::vector<Column> inferColumns(const std::vector<PageElement>& elements, const std::vector<int>& indices) {
    if (indices.empty()) {
        return {};
    }

    std::vector<int> candidates;
    for (const int index : indices) {
        if (isPreferredColumnSeed(elements[static_cast<std::size_t>(index)].block->type)) {
            candidates.push_back(index);
        }
    }
    if (candidates.size() < 2) {
        candidates = indices;
    }

    std::vector<std::pair<double, double>> weighted_widths;
    std::vector<double> heights;
    weighted_widths.reserve(candidates.size());
    heights.reserve(candidates.size());
    for (const int index : candidates) {
        const BBox& bbox = elements[static_cast<std::size_t>(index)].bbox;
        if (bboxWidth(bbox) > kEpsilon) {
            weighted_widths.push_back({bboxWidth(bbox), std::max(1.0, bboxHeight(bbox))});
        }
        if (bboxHeight(bbox) > kEpsilon) {
            heights.push_back(bboxHeight(bbox));
        }
    }
    const double median_width = std::max(kEpsilon, weightedMedian(std::move(weighted_widths)));
    const double median_height = std::max(kEpsilon, median(heights));
    const double regular_width_limit = median_width * kRegularWidthScale;
    const double center_tolerance =
        std::max(1.0, std::min(median_height * 0.75, median_width * kColumnCenterToleranceScale));
    const double minimum_gutter =
        std::max(1.0, std::min(median_height * kMinimumGutterHeightScale, median_width * kMinimumGutterWidthScale));

    std::vector<int> regular;
    for (const int index : candidates) {
        if (bboxWidth(elements[static_cast<std::size_t>(index)].bbox) <= regular_width_limit + kEpsilon) {
            regular.push_back(index);
        }
    }
    if (regular.size() < std::min<std::size_t>(2, candidates.size())) {
        regular = candidates;
    }
    std::sort(regular.begin(), regular.end(), [&](int lhs, int rhs) {
        const BBox& left = elements[static_cast<std::size_t>(lhs)].bbox;
        const BBox& right = elements[static_cast<std::size_t>(rhs)].bbox;
        if (std::abs(centerX(left) - centerX(right)) > kEpsilon) {
            return centerX(left) < centerX(right);
        }
        return left.x0 < right.x0;
    });

    std::vector<ColumnCluster> clusters;
    for (const int index : regular) {
        const BBox& bbox = elements[static_cast<std::size_t>(index)].bbox;
        int best_cluster = -1;
        double best_distance = std::numeric_limits<double>::max();
        for (std::size_t cluster_index = 0; cluster_index < clusters.size(); ++cluster_index) {
            const double distance = std::abs(centerX(bbox) - clusters[cluster_index].center());
            if (distance <= center_tolerance && distance < best_distance) {
                best_cluster = static_cast<int>(cluster_index);
                best_distance = distance;
            }
        }

        const double weight = std::max(1.0, bboxHeight(bbox));
        if (best_cluster < 0) {
            clusters.push_back({{index}, centerX(bbox) * weight, weight});
        } else {
            ColumnCluster& cluster = clusters[static_cast<std::size_t>(best_cluster)];
            cluster.members.push_back(index);
            cluster.weighted_center_sum += centerX(bbox) * weight;
            cluster.vertical_support += weight;
        }
    }

    std::vector<bool> strong(clusters.size(), false);
    int strong_count = 0;
    for (std::size_t index = 0; index < clusters.size(); ++index) {
        strong[index] = clusters[index].members.size() >= 2 || clusters[index].vertical_support >= median_height * 2.0;
        if (strong[index]) {
            ++strong_count;
        }
    }

    std::vector<Column> cluster_columns;
    for (std::size_t cluster_index = 0; cluster_index < clusters.size(); ++cluster_index) {
        std::vector<std::pair<double, double>> starts;
        std::vector<std::pair<double, double>> ends;
        for (const int member : clusters[cluster_index].members) {
            const BBox& bbox = elements[static_cast<std::size_t>(member)].bbox;
            const double weight = std::max(1.0, bboxHeight(bbox));
            starts.push_back({bbox.x0, weight});
            ends.push_back({bbox.x1, weight});
        }
        cluster_columns.push_back({weightedMedian(std::move(starts)), weightedMedian(std::move(ends))});
    }

    std::vector<Column> columns;
    for (std::size_t cluster_index = 0; cluster_index < clusters.size(); ++cluster_index) {
        bool include = strong_count < 2 || strong[cluster_index];
        if (!include && clusters[cluster_index].vertical_support >= median_height * 0.75) {
            include = true;
            const Column& candidate = cluster_columns[cluster_index];
            for (std::size_t strong_index = 0; strong_index < clusters.size(); ++strong_index) {
                if (!strong[strong_index]) {
                    continue;
                }
                const Column& established = cluster_columns[strong_index];
                const bool separated = candidate.x1 + minimum_gutter < established.x0 ||
                                       established.x1 + minimum_gutter < candidate.x0;
                if (!separated) {
                    include = false;
                    break;
                }
            }
        }
        if (include) {
            columns.push_back(cluster_columns[cluster_index]);
        }
    }

    std::sort(columns.begin(), columns.end(), [](const Column& lhs, const Column& rhs) {
        const double lhs_center = (lhs.x0 + lhs.x1) * 0.5;
        const double rhs_center = (rhs.x0 + rhs.x1) * 0.5;
        if (std::abs(lhs_center - rhs_center) > kEpsilon) {
            return lhs_center < rhs_center;
        }
        return lhs.x0 < rhs.x0;
    });

    for (std::size_t index = 0; index + 1 < columns.size();) {
        const double gap = columns[index + 1].x0 - columns[index].x1;
        if (gap <= minimum_gutter) {
            mergeColumnPair(columns, index);
        } else {
            ++index;
        }
    }

    while (columns.size() > kMaximumColumns) {
        std::size_t narrowest_gap_index = 0;
        double narrowest_gap = std::numeric_limits<double>::max();
        for (std::size_t index = 0; index + 1 < columns.size(); ++index) {
            const double gap = columns[index + 1].x0 - columns[index].x1;
            if (gap < narrowest_gap) {
                narrowest_gap = gap;
                narrowest_gap_index = index;
            }
        }
        mergeColumnPair(columns, narrowest_gap_index);
    }

    return columns;
}

ColumnRange locateColumns(const BBox& bbox, const std::vector<Column>& columns) {
    if (columns.size() <= 1) {
        return {};
    }

    std::vector<int> matched;
    for (std::size_t index = 0; index < columns.size(); ++index) {
        const Column& column = columns[index];
        const double overlap = horizontalOverlap(bbox, column);
        const double block_ratio = overlap / std::max(kEpsilon, bboxWidth(bbox));
        const double column_ratio = overlap / std::max(kEpsilon, column.x1 - column.x0);
        if (block_ratio >= 0.10 || column_ratio >= 0.20) {
            matched.push_back(static_cast<int>(index));
        }
    }

    if (!matched.empty()) {
        return {matched.front(), matched.back()};
    }

    int nearest = 0;
    double nearest_distance = std::numeric_limits<double>::max();
    for (std::size_t index = 0; index < columns.size(); ++index) {
        const double column_center = (columns[index].x0 + columns[index].x1) * 0.5;
        const double distance = std::abs(centerX(bbox) - column_center);
        if (distance < nearest_distance) {
            nearest = static_cast<int>(index);
            nearest_distance = distance;
        }
    }
    return {nearest, nearest};
}

void addEdge(std::vector<WeightedEdge>& edges, int from, int to, double confidence, const std::string& reason) {
    if (from == to) {
        return;
    }
    for (WeightedEdge& edge : edges) {
        if (edge.from != from || edge.to != to) {
            continue;
        }
        if (confidence > edge.confidence) {
            edge.confidence = confidence;
            edge.reason = reason;
        }
        return;
    }
    edges.push_back({from, to, confidence, reason, true});
}

bool comesBefore(const std::vector<PageElement>& elements,
                 const std::vector<ColumnRange>& ranges,
                 int column_count,
                 int lhs,
                 int rhs) {
    const PageElement& left = elements[static_cast<std::size_t>(lhs)];
    const PageElement& right = elements[static_cast<std::size_t>(rhs)];
    if (column_count > 1 &&
        ranges[static_cast<std::size_t>(lhs)].first != ranges[static_cast<std::size_t>(rhs)].first) {
        return ranges[static_cast<std::size_t>(lhs)].first < ranges[static_cast<std::size_t>(rhs)].first;
    }
    if (std::abs(left.bbox.y0 - right.bbox.y0) > kEpsilon) {
        return left.bbox.y0 < right.bbox.y0;
    }
    if (std::abs(left.bbox.x0 - right.bbox.x0) > kEpsilon) {
        return left.bbox.x0 < right.bbox.x0;
    }
    return left.layout_block_index < right.layout_block_index;
}

void recordEdgeCounts(const std::vector<WeightedEdge>& edges, document::ReadingOrderTrace& trace) {
    for (const WeightedEdge& edge : edges) {
        ++trace.edge_counts[edge.reason];
    }
}

std::vector<int> findCycleEdges(const std::vector<WeightedEdge>& edges, const std::vector<bool>& emitted) {
    std::vector<int> state(emitted.size(), 0);
    std::vector<int> parent_edge(emitted.size(), -1);
    std::vector<int> cycle;

    std::function<bool(int)> visit = [&](int node) {
        state[static_cast<std::size_t>(node)] = 1;
        for (std::size_t edge_index = 0; edge_index < edges.size(); ++edge_index) {
            const WeightedEdge& edge = edges[edge_index];
            if (!edge.active || edge.from != node || emitted[static_cast<std::size_t>(edge.to)]) {
                continue;
            }
            if (state[static_cast<std::size_t>(edge.to)] == 0) {
                parent_edge[static_cast<std::size_t>(edge.to)] = static_cast<int>(edge_index);
                if (visit(edge.to)) {
                    return true;
                }
            } else if (state[static_cast<std::size_t>(edge.to)] == 1) {
                cycle.push_back(static_cast<int>(edge_index));
                int current = node;
                while (current != edge.to) {
                    const int incoming = parent_edge[static_cast<std::size_t>(current)];
                    if (incoming < 0) {
                        cycle.clear();
                        return false;
                    }
                    cycle.push_back(incoming);
                    current = edges[static_cast<std::size_t>(incoming)].from;
                }
                return true;
            }
        }
        state[static_cast<std::size_t>(node)] = 2;
        return false;
    };

    for (std::size_t node = 0; node < emitted.size(); ++node) {
        if (!emitted[node] && state[node] == 0 && visit(static_cast<int>(node))) {
            return cycle;
        }
    }
    return {};
}

std::vector<int> stableTopologicalOrder(const std::vector<PageElement>& elements,
                                        const std::vector<ColumnRange>& ranges,
                                        int column_count,
                                        std::vector<WeightedEdge> edges,
                                        document::ReadingOrderTrace& trace) {
    std::vector<int> indegree(elements.size(), 0);
    for (const WeightedEdge& edge : edges) {
        ++indegree[static_cast<std::size_t>(edge.to)];
    }

    std::vector<bool> emitted(elements.size(), false);
    std::vector<int> order;
    order.reserve(elements.size());

    while (order.size() < elements.size()) {
        int next = -1;
        for (std::size_t index = 0; index < elements.size(); ++index) {
            if (emitted[index] || indegree[index] != 0) {
                continue;
            }
            if (next < 0 || comesBefore(elements, ranges, column_count, static_cast<int>(index), next)) {
                next = static_cast<int>(index);
            }
        }

        if (next < 0) {
            int weakest_edge = -1;
            for (const int index : findCycleEdges(edges, emitted)) {
                const WeightedEdge& edge = edges[static_cast<std::size_t>(index)];
                if (weakest_edge < 0 || std::tie(edge.confidence, edge.reason, edge.from, edge.to) <
                                            std::tie(edges[static_cast<std::size_t>(weakest_edge)].confidence,
                                                     edges[static_cast<std::size_t>(weakest_edge)].reason,
                                                     edges[static_cast<std::size_t>(weakest_edge)].from,
                                                     edges[static_cast<std::size_t>(weakest_edge)].to)) {
                    weakest_edge = static_cast<int>(index);
                }
            }

            if (weakest_edge >= 0) {
                WeightedEdge& edge = edges[static_cast<std::size_t>(weakest_edge)];
                edge.active = false;
                --indegree[static_cast<std::size_t>(edge.to)];
                trace.cycle_breaks.push_back({
                    elements[static_cast<std::size_t>(edge.from)].block->id,
                    elements[static_cast<std::size_t>(edge.to)].block->id,
                    edge.reason,
                    edge.confidence,
                });
                continue;
            }

            for (std::size_t index = 0; index < elements.size(); ++index) {
                if (!emitted[index] &&
                    (next < 0 || comesBefore(elements, ranges, column_count, static_cast<int>(index), next))) {
                    next = static_cast<int>(index);
                }
            }
        }

        emitted[static_cast<std::size_t>(next)] = true;
        order.push_back(next);
        for (WeightedEdge& edge : edges) {
            if (edge.active && edge.from == next && !emitted[static_cast<std::size_t>(edge.to)]) {
                --indegree[static_cast<std::size_t>(edge.to)];
            }
        }
    }

    return order;
}

BandOrder orderBand(const std::vector<PageElement>& group_elements,
                    const std::vector<int>& group_indices,
                    const std::string& group,
                    int band_index,
                    document::ReadingOrderTrace& trace) {
    BandOrder result;
    if (group_indices.empty()) {
        return result;
    }

    std::vector<PageElement> elements;
    elements.reserve(group_indices.size());
    for (const int index : group_indices) {
        elements.push_back(group_elements[static_cast<std::size_t>(index)]);
    }

    std::vector<int> local_indices(elements.size());
    for (std::size_t index = 0; index < elements.size(); ++index) {
        local_indices[index] = static_cast<int>(index);
    }
    const std::vector<Column> columns = inferColumns(elements, local_indices);
    result.column_count = std::max(1, static_cast<int>(columns.size()));
    result.ranges.reserve(elements.size());
    for (const PageElement& element : elements) {
        result.ranges.push_back(locateColumns(element.bbox, columns));
        const ColumnRange& range = result.ranges.back();
        trace.placements.push_back({
            element.block->id,
            group,
            band_index,
            range.first,
            range.last,
        });
    }

    std::vector<WeightedEdge> edges;
    std::vector<std::vector<int>> by_column(static_cast<std::size_t>(result.column_count));
    std::vector<int> spanning;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const ColumnRange& range = result.ranges[index];
        if (range.spanning()) {
            spanning.push_back(static_cast<int>(index));
        } else {
            by_column[static_cast<std::size_t>(range.first)].push_back(static_cast<int>(index));
        }
    }

    const auto vertical_order = [&](int lhs, int rhs) {
        const BBox& left = elements[static_cast<std::size_t>(lhs)].bbox;
        const BBox& right = elements[static_cast<std::size_t>(rhs)].bbox;
        if (std::abs(centerY(left) - centerY(right)) > kEpsilon) {
            return centerY(left) < centerY(right);
        }
        if (std::abs(left.x0 - right.x0) > kEpsilon) {
            return left.x0 < right.x0;
        }
        return elements[static_cast<std::size_t>(lhs)].layout_block_index <
               elements[static_cast<std::size_t>(rhs)].layout_block_index;
    };

    int previous_nonempty_column = -1;
    for (std::size_t column_index = 0; column_index < by_column.size(); ++column_index) {
        std::vector<int>& column = by_column[column_index];
        std::sort(column.begin(), column.end(), vertical_order);
        for (std::size_t index = 1; index < column.size(); ++index) {
            const int before = column[index - 1];
            const int after = column[index];
            if (std::abs(centerY(elements[static_cast<std::size_t>(before)].bbox) -
                         centerY(elements[static_cast<std::size_t>(after)].bbox)) > kEpsilon) {
                addEdge(edges, before, after, kSameColumnConfidence, "same_column");
            }
        }
        if (!column.empty() && previous_nonempty_column >= 0) {
            const std::vector<int>& previous = by_column[static_cast<std::size_t>(previous_nonempty_column)];
            addEdge(edges, previous.back(), column.front(), kColumnFlowConfidence, "column_flow");
        }
        if (!column.empty()) {
            previous_nonempty_column = static_cast<int>(column_index);
        }
    }

    std::sort(spanning.begin(), spanning.end(), vertical_order);
    for (std::size_t index = 1; index < spanning.size(); ++index) {
        addEdge(edges, spanning[index - 1], spanning[index], kSpanningGeometryConfidence, "spanning_geometry");
    }
    for (const int spanning_index : spanning) {
        const ColumnRange& range = result.ranges[static_cast<std::size_t>(spanning_index)];
        const double spanning_center = centerY(elements[static_cast<std::size_t>(spanning_index)].bbox);
        for (int column_index = range.first; column_index <= range.last; ++column_index) {
            int nearest_above = -1;
            int nearest_below = -1;
            for (const int candidate : by_column[static_cast<std::size_t>(column_index)]) {
                if (centerY(elements[static_cast<std::size_t>(candidate)].bbox) < spanning_center) {
                    nearest_above = candidate;
                } else {
                    nearest_below = candidate;
                    break;
                }
            }
            if (nearest_above >= 0) {
                addEdge(edges, nearest_above, spanning_index, kSpanningGeometryConfidence, "spanning_geometry");
            }
            if (nearest_below >= 0) {
                addEdge(edges, spanning_index, nearest_below, kSpanningGeometryConfidence, "spanning_geometry");
            }
        }
    }

    std::vector<int> hinted;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (elements[index].block->reading_order_hint >= 0) {
            hinted.push_back(static_cast<int>(index));
        }
    }
    std::stable_sort(hinted.begin(), hinted.end(), [&](int lhs, int rhs) {
        return elements[static_cast<std::size_t>(lhs)].block->reading_order_hint <
               elements[static_cast<std::size_t>(rhs)].block->reading_order_hint;
    });
    for (std::size_t index = 1; index < hinted.size(); ++index) {
        const int before = hinted[index - 1];
        const int after = hinted[index];
        if (elements[static_cast<std::size_t>(before)].block->reading_order_hint !=
            elements[static_cast<std::size_t>(after)].block->reading_order_hint) {
            addEdge(edges, before, after, kModelHintConfidence, "model_hint");
        }
    }

    std::map<std::string, int> local_by_id;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        local_by_id[elements[index].block->id] = static_cast<int>(index);
    }
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const std::string& target_id = elements[index].block->related_block_id;
        const auto target = local_by_id.find(target_id);
        if (!target_id.empty() && target != local_by_id.end()) {
            addEdge(edges, target->second, static_cast<int>(index), kSameColumnConfidence, "caption_target");
        }
    }

    recordEdgeCounts(edges, trace);
    const std::vector<int> local_order =
        stableTopologicalOrder(elements, result.ranges, result.column_count, std::move(edges), trace);
    result.indices.reserve(local_order.size());
    for (const int local_index : local_order) {
        result.indices.push_back(group_indices[static_cast<std::size_t>(local_index)]);
    }
    return result;
}

bool spansAllColumns(const BBox& bbox, const std::vector<Column>& columns) {
    if (columns.size() < 2) {
        return false;
    }
    const ColumnRange range = locateColumns(bbox, columns);
    const double content_width = columns.back().x1 - columns.front().x0;
    return range.first == 0 && range.last == static_cast<int>(columns.size() - 1) &&
           bboxWidth(bbox) >= content_width * 0.55;
}

std::vector<int> predictGroupOrder(const std::vector<PageElement>& elements,
                                   const std::string& group,
                                   int& next_band_index,
                                   document::ReadingOrderTrace& trace) {
    if (elements.empty()) {
        return {};
    }

    std::vector<int> all_indices(elements.size());
    for (std::size_t index = 0; index < elements.size(); ++index) {
        all_indices[index] = static_cast<int>(index);
    }
    const std::vector<Column> global_columns = inferColumns(elements, all_indices);

    std::vector<int> separators;
    std::vector<int> remaining;
    for (const int index : all_indices) {
        if (spansAllColumns(elements[static_cast<std::size_t>(index)].bbox, global_columns)) {
            separators.push_back(index);
        } else {
            remaining.push_back(index);
        }
    }
    separators = sortedByPosition(elements, std::move(separators));

    std::vector<int> order;
    std::vector<bool> consumed(elements.size(), false);
    const auto append_band = [&](const std::vector<int>& band, std::vector<int>& destination) {
        if (band.empty()) {
            return;
        }
        BandOrder ordered = orderBand(elements, band, group, next_band_index, trace);
        destination.insert(destination.end(), ordered.indices.begin(), ordered.indices.end());
        ++next_band_index;
    };

    for (const int separator : separators) {
        std::vector<int> band;
        const double separator_y = centerY(elements[static_cast<std::size_t>(separator)].bbox);
        for (const int index : remaining) {
            if (!consumed[static_cast<std::size_t>(index)] &&
                centerY(elements[static_cast<std::size_t>(index)].bbox) < separator_y) {
                band.push_back(index);
                consumed[static_cast<std::size_t>(index)] = true;
            }
        }
        append_band(band, order);

        trace.placements.push_back({
            elements[static_cast<std::size_t>(separator)].block->id,
            group,
            next_band_index,
            0,
            std::max(0, static_cast<int>(global_columns.size()) - 1),
        });
        order.push_back(separator);
        consumed[static_cast<std::size_t>(separator)] = true;
        ++next_band_index;
    }

    std::vector<int> tail;
    for (const int index : remaining) {
        if (!consumed[static_cast<std::size_t>(index)]) {
            tail.push_back(index);
        }
    }
    append_band(tail, order);
    return order;
}

std::vector<int> placeCaptionsAfterTargets(const std::vector<PageElement>& elements, const std::vector<int>& order) {
    std::vector<int> linked_captions;
    for (const int index : order) {
        const LayoutBlock& block = *elements[static_cast<std::size_t>(index)].block;
        if (!block.related_block_id.empty()) {
            linked_captions.push_back(index);
        }
    }
    if (linked_captions.empty()) {
        return order;
    }

    std::vector<int> result;
    for (const int index : order) {
        if (std::find(linked_captions.begin(), linked_captions.end(), index) != linked_captions.end()) {
            continue;
        }
        result.push_back(index);
        const std::string& target_id = elements[static_cast<std::size_t>(index)].block->id;
        for (const int caption_index : linked_captions) {
            const LayoutBlock& caption = *elements[static_cast<std::size_t>(caption_index)].block;
            if (caption.related_block_id == target_id) {
                result.push_back(caption_index);
            }
        }
    }
    for (const int caption_index : linked_captions) {
        if (std::find(result.begin(), result.end(), caption_index) == result.end()) {
            result.push_back(caption_index);
        }
    }
    return result;
}

void appendItems(const std::vector<PageElement>& elements,
                 const std::vector<int>& order,
                 document::PageReadingOrder& reading_order) {
    for (const int index : order) {
        const PageElement& element = elements[static_cast<std::size_t>(index)];
        reading_order.items.push_back({
            element.block->id,
            element.layout_block_index,
            static_cast<int>(reading_order.items.size()),
        });
    }
}

std::vector<PageElement> collectElements(const document::PageLayout& layout,
                                         const std::set<LayoutBlockType>& types,
                                         bool include_matching_types) {
    std::vector<PageElement> elements;
    for (std::size_t index = 0; index < layout.blocks.size(); ++index) {
        const LayoutBlock& block = layout.blocks[index];
        const bool matches = types.find(block.type) != types.end();
        if (matches != include_matching_types) {
            continue;
        }
        elements.push_back({
            static_cast<int>(index),
            &block,
            block.bbox,
        });
    }
    return elements;
}

} // namespace

bool DoclingLikeReadingOrderBackend::order(const ReadingOrderRequest& request, ReadingOrderResult& result) const {
    result.reading_order = {};
    result.reading_order.page_index = request.layout.page_index;
    result.reading_order.page_number = request.layout.page_number;
    result.reading_order.trace.algorithm = "band-column-topological-v2";

    const std::set<LayoutBlockType> header_types{LayoutBlockType::Header};
    const std::set<LayoutBlockType> footer_types{LayoutBlockType::Footer};
    const std::set<LayoutBlockType> furniture_types{LayoutBlockType::Header, LayoutBlockType::Footer};

    const std::vector<PageElement> headers = collectElements(request.layout, header_types, true);
    const std::vector<PageElement> body = collectElements(request.layout, furniture_types, false);
    const std::vector<PageElement> footers = collectElements(request.layout, footer_types, true);

    int next_band_index = 0;
    appendItems(headers,
                predictGroupOrder(headers, "header", next_band_index, result.reading_order.trace),
                result.reading_order);
    const std::vector<int> body_order =
        placeCaptionsAfterTargets(body, predictGroupOrder(body, "body", next_band_index, result.reading_order.trace));
    appendItems(body, body_order, result.reading_order);
    appendItems(footers,
                predictGroupOrder(footers, "footer", next_band_index, result.reading_order.trace),
                result.reading_order);

    return true;
}

} // namespace doc_parser::reading_order
