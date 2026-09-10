#include "document/page_rotation.h"

#include "document/parsed_document.h"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace doc_parser::document {
namespace {
BBox rotate(const BBox& box, int width, int height, int degrees) {
    switch (degrees) {
    case 90:
        return {height - box.y1, box.x0, height - box.y0, box.x1};
    case 180:
        return {width - box.x1, height - box.y1, width - box.x0, height - box.y0};
    case 270:
        return {box.y0, width - box.x1, box.y1, width - box.x0};
    default:
        return box;
    }
}

using RotationsByPage = std::map<std::string, const PageRotation*>;

void restoreRefs(std::vector<SourceReference>& refs, const RotationsByPage& rotations) {
    for (auto& ref : refs) {
        const auto found = rotations.find(ref.page_id);
        if (found != rotations.end())
            ref.bbox = found->second->toSource(ref.bbox);
    }
}

void restoreRows(std::vector<TableRow>& rows, const PageRotation& rotation, const RotationsByPage& rotations) {
    for (auto& row : rows) {
        row.bbox = rotation.toSource(row.bbox);
        for (auto& cell : row.cells) {
            cell.bbox = rotation.toSource(cell.bbox);
            restoreRefs(cell.source_refs, rotations);
        }
    }
}
} // namespace

PageRotation::PageRotation(int source_width, int source_height, int clockwise_degrees)
    : width_(source_width), height_(source_height), degrees_(clockwise_degrees) {
    if (degrees_ != 0 && degrees_ != 90 && degrees_ != 180 && degrees_ != 270)
        throw std::invalid_argument("page correction must be 0, 90, 180 or 270 degrees");
    if (degrees_ != 0 && (width_ <= 0 || height_ <= 0))
        throw std::invalid_argument("rotated page dimensions must be positive");
}

int PageRotation::workingWidth() const { return degrees_ % 180 == 0 ? width_ : height_; }
int PageRotation::workingHeight() const { return degrees_ % 180 == 0 ? height_ : width_; }
BBox PageRotation::toWorking(const BBox& box) const { return rotate(box, width_, height_, degrees_); }
BBox PageRotation::toSource(const BBox& box) const {
    return rotate(box, workingWidth(), workingHeight(), (360 - degrees_) % 360);
}

void PageRotation::textToWorking(PageText& text) const {
    for (auto& line : text.lines) {
        line.bbox = toWorking(line.bbox);
        for (auto& span : line.spans)
            span.bbox = toWorking(span.bbox);
    }
}

void restoreSourceCoordinates(ParsedDocument& document,
                              PipelineArtifacts& artifacts,
                              const std::vector<PageRotation>& rotations) {
    if (rotations.size() != artifacts.pages.size() || rotations.size() != document.pages.size())
        throw std::invalid_argument("page rotation count does not match assembled pages");
    if (std::none_of(rotations.begin(), rotations.end(), [](const auto& rotation) { return rotation.degrees() != 0; }))
        return;
    RotationsByPage by_page;
    for (std::size_t i = 0; i < rotations.size(); ++i)
        by_page.emplace(document.pages[i].id, &rotations[i]);
    for (std::size_t i = 0; i < rotations.size(); ++i) {
        const auto& rotation = rotations[i];
        auto& page = artifacts.pages[i];
        document.pages[i].width = page.image.width = rotation.sourceWidth();
        document.pages[i].height = page.image.height = rotation.sourceHeight();
        for (auto& line : page.text.lines) {
            line.bbox = rotation.toSource(line.bbox);
            for (auto& span : line.spans)
                span.bbox = rotation.toSource(span.bbox);
        }
        for (auto& block : page.layout.blocks)
            block.bbox = rotation.toSource(block.bbox);
        for (auto& table : page.tables.tables) {
            table.bbox = rotation.toSource(table.bbox);
            for (auto& column : table.columns)
                column.bbox = rotation.toSource(column.bbox);
            for (auto& object : table.structure_objects)
                object.bbox = rotation.toSource(object.bbox);
            restoreRows(table.rows, rotation, by_page);
        }
    }
    for (auto& block : document.blocks) {
        const auto found = by_page.find(block.page_id);
        if (found != by_page.end()) {
            block.bbox = found->second->toSource(block.bbox);
            restoreRows(block.table_rows, *found->second, by_page);
        }
        restoreRefs(block.source_refs, by_page);
    }
}
} // namespace doc_parser::document
