#include "reading_order/reading_order_backend.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <utility>
#include <vector>

namespace doc_parser::reading_order {
namespace {

using document::BBox;
using document::LayoutBlock;
using document::LayoutBlockType;

constexpr double kEpsilon = 1.0e-3;
constexpr double kHorizontalDilationThresholdNorm = 0.15;
constexpr double kSpanningWidthThresholdNorm = 0.65;

struct PageElement {
    int layout_block_index = -1;
    const LayoutBlock* block = nullptr;
    BBox bbox;
};

struct ReadingGraph {
    std::vector<std::vector<int>> up;
    std::vector<std::vector<int>> down;
    std::vector<int> heads;
};

class SpatialIndex {
public:
    explicit SpatialIndex(const std::vector<PageElement>& elements) : elements_(elements) {}

    std::vector<int> intersecting(const BBox& query) const {
        std::vector<int> matches;
        for (std::size_t index = 0; index < elements_.size(); ++index) {
            if (overlaps(elements_[index].bbox, query)) {
                matches.push_back(static_cast<int>(index));
            }
        }
        return matches;
    }

private:
    static bool overlaps(const BBox& lhs, const BBox& rhs) {
        return lhs.x0 < rhs.x1 && lhs.x1 > rhs.x0 && lhs.y0 < rhs.y1 && lhs.y1 > rhs.y0;
    }

    const std::vector<PageElement>& elements_;
};

double pageWidth(const document::PageArtifact& page, const std::vector<PageElement>& elements) {
    if (page.width > 0) {
        return static_cast<double>(page.width);
    }

    double width = 1.0;
    for (const auto& element : elements) {
        width = std::max(width, element.bbox.x1);
    }
    return width;
}

double horizontalOverlap(const BBox& lhs, const BBox& rhs) {
    return std::max(0.0, std::min(lhs.x1, rhs.x1) - std::max(lhs.x0, rhs.x0));
}

bool overlapsHorizontally(const BBox& lhs, const BBox& rhs) { return horizontalOverlap(lhs, rhs) > kEpsilon; }

bool isStrictlyAbove(const BBox& lhs, const BBox& rhs) { return lhs.y1 <= rhs.y0 + kEpsilon; }

bool comesBeforeForHeadSort(const PageElement& lhs, const PageElement& rhs) {
    if (overlapsHorizontally(lhs.bbox, rhs.bbox)) {
        if (std::abs(lhs.bbox.y0 - rhs.bbox.y0) > kEpsilon) {
            return lhs.bbox.y0 < rhs.bbox.y0;
        }
        return lhs.bbox.x0 < rhs.bbox.x0;
    }
    if (std::abs(lhs.bbox.x0 - rhs.bbox.x0) > kEpsilon) {
        return lhs.bbox.x0 < rhs.bbox.x0;
    }
    return lhs.bbox.y0 < rhs.bbox.y0;
}

bool hasSequenceInterruption(const SpatialIndex& index,
                             const std::vector<PageElement>& elements,
                             int above_index,
                             int below_index) {
    const BBox& above = elements[static_cast<std::size_t>(above_index)].bbox;
    const BBox& below = elements[static_cast<std::size_t>(below_index)].bbox;
    const BBox query{
        std::min(above.x0, below.x0) - 1.0,
        above.y1,
        std::max(above.x1, below.x1) + 1.0,
        below.y0,
    };

    for (const int candidate_index : index.intersecting(query)) {
        if (candidate_index == above_index || candidate_index == below_index) {
            continue;
        }

        const BBox& candidate = elements[static_cast<std::size_t>(candidate_index)].bbox;
        if ((overlapsHorizontally(above, candidate) || overlapsHorizontally(below, candidate)) &&
            isStrictlyAbove(above, candidate) && isStrictlyAbove(candidate, below)) {
            return true;
        }
    }

    return false;
}

ReadingGraph buildGraph(const std::vector<PageElement>& elements) {
    ReadingGraph graph;
    graph.up.resize(elements.size());
    graph.down.resize(elements.size());
    const SpatialIndex index(elements);

    for (std::size_t below_index = 0; below_index < elements.size(); ++below_index) {
        const BBox& below = elements[below_index].bbox;
        const BBox query{
            below.x0 - 0.1,
            0.0,
            below.x1 + 0.1,
            below.y0,
        };

        for (const int above_index : index.intersecting(query)) {
            if (above_index == static_cast<int>(below_index)) {
                continue;
            }

            const BBox& above = elements[static_cast<std::size_t>(above_index)].bbox;
            if (!isStrictlyAbove(above, below) || !overlapsHorizontally(above, below)) {
                continue;
            }

            if (hasSequenceInterruption(index, elements, above_index, static_cast<int>(below_index))) {
                continue;
            }

            graph.down[static_cast<std::size_t>(above_index)].push_back(static_cast<int>(below_index));
            graph.up[below_index].push_back(above_index);
        }
    }

    return graph;
}

bool wouldOverlapAny(const std::vector<PageElement>& original, std::size_t current_index, const BBox& bbox) {
    for (std::size_t index = 0; index < original.size(); ++index) {
        if (index == current_index) {
            continue;
        }
        if (bbox.x0 < original[index].bbox.x1 && bbox.x1 > original[index].bbox.x0 &&
            bbox.y0 < original[index].bbox.y1 && bbox.y1 > original[index].bbox.y0) {
            return true;
        }
    }
    return false;
}

std::vector<PageElement> dilateHorizontally(const document::PageArtifact& page,
                                            const std::vector<PageElement>& elements,
                                            const ReadingGraph& graph) {
    std::vector<PageElement> dilated = elements;
    const double threshold = kHorizontalDilationThresholdNorm * pageWidth(page, elements);

    for (std::size_t index = 0; index < dilated.size(); ++index) {
        BBox candidate = dilated[index].bbox;
        bool changed = false;

        if (!graph.up[index].empty()) {
            const BBox& up = elements[static_cast<std::size_t>(graph.up[index].front())].bbox;
            const double x0 = std::min(candidate.x0, up.x0);
            const double x1 = std::max(candidate.x1, up.x1);
            if ((candidate.x0 - x0) <= threshold && (x1 - candidate.x1) <= threshold) {
                candidate.x0 = x0;
                candidate.x1 = x1;
                changed = true;
            }
        }

        if (!graph.down[index].empty()) {
            const BBox& down = elements[static_cast<std::size_t>(graph.down[index].front())].bbox;
            const double x0 = std::min(candidate.x0, down.x0);
            const double x1 = std::max(candidate.x1, down.x1);
            if ((candidate.x0 - x0) <= threshold && (x1 - candidate.x1) <= threshold) {
                candidate.x0 = x0;
                candidate.x1 = x1;
                changed = true;
            }
        }

        if (changed && !wouldOverlapAny(elements, index, candidate)) {
            dilated[index].bbox = candidate;
        }
    }

    return dilated;
}

void sortGraph(const std::vector<PageElement>& elements, ReadingGraph& graph) {
    for (auto& down : graph.down) {
        std::sort(down.begin(), down.end(), [&](int lhs, int rhs) {
            return comesBeforeForHeadSort(elements[static_cast<std::size_t>(lhs)],
                                          elements[static_cast<std::size_t>(rhs)]);
        });
        down.erase(std::unique(down.begin(), down.end()), down.end());
    }

    for (auto& up : graph.up) {
        std::sort(up.begin(), up.end(), [&](int lhs, int rhs) {
            return comesBeforeForHeadSort(elements[static_cast<std::size_t>(lhs)],
                                          elements[static_cast<std::size_t>(rhs)]);
        });
        up.erase(std::unique(up.begin(), up.end()), up.end());
    }

    graph.heads.clear();
    for (std::size_t index = 0; index < graph.up.size(); ++index) {
        if (graph.up[index].empty()) {
            graph.heads.push_back(static_cast<int>(index));
        }
    }

    std::sort(graph.heads.begin(), graph.heads.end(), [&](int lhs, int rhs) {
        return comesBeforeForHeadSort(elements[static_cast<std::size_t>(lhs)], elements[static_cast<std::size_t>(rhs)]);
    });
}

int climbToUnvisitedHead(int index, const ReadingGraph& graph, const std::vector<bool>& visited) {
    int current = index;
    while (true) {
        bool moved = false;
        for (const int parent : graph.up[static_cast<std::size_t>(current)]) {
            if (!visited[static_cast<std::size_t>(parent)]) {
                current = parent;
                moved = true;
                break;
            }
        }
        if (!moved) {
            return current;
        }
    }
}

std::vector<int> findOrder(const std::vector<PageElement>& elements, const ReadingGraph& graph) {
    std::vector<int> order;
    std::vector<bool> visited(elements.size(), false);

    for (const int head : graph.heads) {
        if (visited[static_cast<std::size_t>(head)]) {
            continue;
        }

        order.push_back(head);
        visited[static_cast<std::size_t>(head)] = true;

        std::vector<std::pair<const std::vector<int>*, std::size_t>> stack;
        stack.push_back({&graph.down[static_cast<std::size_t>(head)], 0});

        while (!stack.empty()) {
            const std::vector<int>& children = *stack.back().first;
            std::size_t offset = stack.back().second;
            bool found = false;

            while (offset < children.size()) {
                const int child = climbToUnvisitedHead(children[offset], graph, visited);
                stack.back().second = offset + 1;
                ++offset;

                if (!visited[static_cast<std::size_t>(child)]) {
                    order.push_back(child);
                    visited[static_cast<std::size_t>(child)] = true;
                    stack.push_back({&graph.down[static_cast<std::size_t>(child)], 0});
                    found = true;
                    break;
                }
            }

            if (!found) {
                stack.pop_back();
            }
        }
    }

    std::vector<int> remaining;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (!visited[index]) {
            remaining.push_back(static_cast<int>(index));
        }
    }
    std::sort(remaining.begin(), remaining.end(), [&](int lhs, int rhs) {
        return comesBeforeForHeadSort(elements[static_cast<std::size_t>(lhs)], elements[static_cast<std::size_t>(rhs)]);
    });
    order.insert(order.end(), remaining.begin(), remaining.end());

    return order;
}

std::vector<int> predictLocalOrder(const document::PageArtifact& page, const std::vector<PageElement>& elements) {
    if (elements.empty()) {
        return {};
    }

    ReadingGraph graph = buildGraph(elements);
    const std::vector<PageElement> dilated = dilateHorizontally(page, elements, graph);
    graph = buildGraph(dilated);
    sortGraph(elements, graph);
    return findOrder(elements, graph);
}

std::vector<int> orderSubset(const document::PageArtifact& page,
                             const std::vector<PageElement>& elements,
                             const std::vector<int>& indices) {
    std::vector<PageElement> subset;
    subset.reserve(indices.size());
    for (const int index : indices) {
        subset.push_back(elements[static_cast<std::size_t>(index)]);
    }

    std::vector<int> result;
    for (const int local_index : predictLocalOrder(page, subset)) {
        result.push_back(indices[static_cast<std::size_t>(local_index)]);
    }
    return result;
}

std::vector<int> predictGroupOrder(const document::PageArtifact& page, const std::vector<PageElement>& elements) {
    if (elements.empty()) {
        return {};
    }

    const bool has_model_order = std::all_of(elements.begin(), elements.end(), [](const PageElement& element) {
        return element.block->reading_order_hint >= 0;
    });
    if (has_model_order) {
        std::vector<int> order(elements.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int lhs, int rhs) {
            return elements[static_cast<std::size_t>(lhs)].block->reading_order_hint <
                   elements[static_cast<std::size_t>(rhs)].block->reading_order_hint;
        });
        return order;
    }

    const double width = pageWidth(page, elements);
    std::vector<int> spanning;
    std::vector<int> remaining;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const double element_width = std::max(0.0, elements[index].bbox.x1 - elements[index].bbox.x0);
        if (element_width >= width * kSpanningWidthThresholdNorm) {
            spanning.push_back(static_cast<int>(index));
        } else {
            remaining.push_back(static_cast<int>(index));
        }
    }
    if (spanning.empty()) {
        return predictLocalOrder(page, elements);
    }

    const auto topLeftOrder = [&](int lhs, int rhs) {
        const BBox& lhs_bbox = elements[static_cast<std::size_t>(lhs)].bbox;
        const BBox& rhs_bbox = elements[static_cast<std::size_t>(rhs)].bbox;
        if (std::abs(lhs_bbox.y0 - rhs_bbox.y0) > kEpsilon) {
            return lhs_bbox.y0 < rhs_bbox.y0;
        }
        return lhs_bbox.x0 < rhs_bbox.x0;
    };
    std::sort(spanning.begin(), spanning.end(), topLeftOrder);

    std::vector<int> result;
    std::vector<bool> consumed(elements.size(), false);
    for (const int spanning_index : spanning) {
        std::vector<int> band;
        const double separator_y = elements[static_cast<std::size_t>(spanning_index)].bbox.y0;
        for (const int index : remaining) {
            const BBox& bbox = elements[static_cast<std::size_t>(index)].bbox;
            const double center_y = (bbox.y0 + bbox.y1) * 0.5;
            if (!consumed[static_cast<std::size_t>(index)] && center_y < separator_y) {
                band.push_back(index);
                consumed[static_cast<std::size_t>(index)] = true;
            }
        }
        const std::vector<int> ordered_band = orderSubset(page, elements, band);
        result.insert(result.end(), ordered_band.begin(), ordered_band.end());
        result.push_back(spanning_index);
        consumed[static_cast<std::size_t>(spanning_index)] = true;
    }

    std::vector<int> tail;
    for (const int index : remaining) {
        if (!consumed[static_cast<std::size_t>(index)]) {
            tail.push_back(index);
        }
    }
    const std::vector<int> ordered_tail = orderSubset(page, elements, tail);
    result.insert(result.end(), ordered_tail.begin(), ordered_tail.end());
    return result;
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

    const std::set<LayoutBlockType> header_types{LayoutBlockType::Header};
    const std::set<LayoutBlockType> footer_types{LayoutBlockType::Footer};
    const std::set<LayoutBlockType> furniture_types{LayoutBlockType::Header, LayoutBlockType::Footer};

    const std::vector<PageElement> headers = collectElements(request.layout, header_types, true);
    const std::vector<PageElement> body = collectElements(request.layout, furniture_types, false);
    const std::vector<PageElement> footers = collectElements(request.layout, footer_types, true);

    appendItems(headers, predictGroupOrder(request.page, headers), result.reading_order);
    const std::vector<int> body_order = placeCaptionsAfterTargets(body, predictGroupOrder(request.page, body));
    appendItems(body, body_order, result.reading_order);
    appendItems(footers, predictGroupOrder(request.page, footers), result.reading_order);

    return true;
}

} // namespace doc_parser::reading_order
