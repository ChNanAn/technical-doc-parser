#include "layout/layout_postprocessing.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

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

} // namespace doc_parser::layout::detail
