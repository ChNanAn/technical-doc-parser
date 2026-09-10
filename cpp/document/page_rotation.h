#pragma once

#include "document/text_model.h"

#include <vector>

namespace doc_parser::document {

struct ParsedDocument;
struct PipelineArtifacts;

// Pixel-edge (xyxy) coordinates, independent of image libraries. Detection is
// separate: the current built-in OCR backend only proposes 0 or 180 degrees.
class PageRotation {
public:
    PageRotation(int source_width, int source_height, int clockwise_degrees);
    int degrees() const { return degrees_; }
    int sourceWidth() const { return width_; }
    int sourceHeight() const { return height_; }
    int workingWidth() const;
    int workingHeight() const;
    BBox toWorking(const BBox& box) const;
    BBox toSource(const BBox& box) const;
    void textToWorking(PageText& text) const;

private:
    int width_, height_, degrees_;
};

// Call once after assembly, before exposing any results. References are mapped
// using their own page IDs, including references to a different rotated page.
void restoreSourceCoordinates(ParsedDocument& document,
                              PipelineArtifacts& artifacts,
                              const std::vector<PageRotation>& rotations);

} // namespace doc_parser::document
