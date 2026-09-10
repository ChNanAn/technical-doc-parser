#include "document/page_rotation.h"
#include "document/parsed_document.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace {
using namespace doc_parser::document;

void expectBox(const BBox& a, const BBox& b) {
    EXPECT_DOUBLE_EQ(a.x0, b.x0);
    EXPECT_DOUBLE_EQ(a.y0, b.y0);
    EXPECT_DOUBLE_EQ(a.x1, b.x1);
    EXPECT_DOUBLE_EQ(a.y1, b.y1);
}

TEST(PageRotationTest, RotatesPixelEdgesAndInvertsAllQuadrants) {
    const BBox original{10, 20, 30, 50};
    const std::vector<BBox> expected{{10, 20, 30, 50}, {10, 10, 40, 30}, {70, 10, 90, 40}, {20, 70, 50, 90}};
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const PageRotation rotation(100, 60, quadrant * 90);
        expectBox(rotation.toWorking(original), expected[quadrant]);
        expectBox(rotation.toSource(expected[quadrant]), original);
        expectBox(
            rotation.toSource(
                {0, 0, static_cast<double>(rotation.workingWidth()), static_cast<double>(rotation.workingHeight())}),
            {0, 0, 100, 60});
        const BBox fractional{0.25, 1.75, 99.5, 58.125};
        expectBox(rotation.toSource(rotation.toWorking(fractional)), fractional);
    }
}

TEST(PageRotationTest, RejectsUnsupportedCorrectionsAndInvalidRotatedDimensions) {
    EXPECT_THROW(PageRotation(100, 60, 45), std::invalid_argument);
    EXPECT_THROW(PageRotation(0, 60, 180), std::invalid_argument);
    EXPECT_THROW(PageRotation(100, -1, 90), std::invalid_argument);
    EXPECT_NO_THROW(PageRotation(0, 0, 0));
}

TEST(PageRotationTest, RestoresAllNestedGeometryAndUsesEachReferencePage) {
    const BBox working{10, 20, 30, 50};
    const BBox expected{70, 10, 90, 40};
    const BBox other_working{10, 10, 40, 30};
    const BBox other_expected{10, 20, 30, 50};
    const std::vector<PageRotation> rotations{{100, 60, 180}, {100, 60, 90}};
    ParsedDocument doc;
    doc.pages.resize(2);
    doc.pages[0].id = "page_1";
    doc.pages[1].id = "page_2";
    PipelineArtifacts artifacts;
    artifacts.pages.resize(2);
    auto& page = artifacts.pages[0];
    TextLine line;
    line.text = "ordered text";
    line.bbox = working;
    line.spans.push_back({"ordered text", working, TextSource::Ocr, 0.95});
    page.text.lines.push_back(line);
    LayoutBlock layout;
    layout.bbox = working;
    layout.text_line_indices = {0};
    page.layout.blocks.push_back(layout);
    TableCell cell;
    cell.bbox = working;
    cell.row_span = 2;
    cell.column_span = 3;
    cell.text = "cell";
    cell.source_refs = {{"page_1", working, "local", TextSource::Ocr},
                        {"page_2", other_working, "remote", TextSource::PdfTextLayer}};
    TableRow row;
    row.bbox = working;
    row.cells.push_back(cell);
    Table table;
    table.bbox = working;
    table.rows.push_back(row);
    table.columns.push_back({0, working, 0.9});
    table.structure_objects.push_back({"table row", working, 0.8});
    table.continuation_group_id = "group";
    page.tables.tables.push_back(table);
    DocumentBlock block;
    block.page_id = "page_1";
    block.bbox = working;
    block.table_rows.push_back(row);
    block.source_refs = cell.source_refs;
    doc.blocks.push_back(block);
    restoreSourceCoordinates(doc, artifacts, rotations);
    expectBox(page.text.lines[0].bbox, expected);
    expectBox(page.text.lines[0].spans[0].bbox, expected);
    EXPECT_EQ(page.text.lines[0].spans[0].text, "ordered text");
    EXPECT_DOUBLE_EQ(page.text.lines[0].spans[0].confidence, 0.95);
    expectBox(page.layout.blocks[0].bbox, expected);
    const auto& restored = page.tables.tables[0];
    expectBox(restored.bbox, expected);
    expectBox(restored.columns[0].bbox, expected);
    expectBox(restored.structure_objects[0].bbox, expected);
    expectBox(restored.rows[0].bbox, expected);
    expectBox(restored.rows[0].cells[0].bbox, expected);
    expectBox(restored.rows[0].cells[0].source_refs[1].bbox, other_expected);
    EXPECT_EQ(restored.rows[0].cells[0].row_span, 2);
    EXPECT_EQ(restored.rows[0].cells[0].column_span, 3);
    EXPECT_EQ(restored.continuation_group_id, "group");
    expectBox(doc.blocks[0].bbox, expected);
    expectBox(doc.blocks[0].table_rows[0].bbox, expected);
    expectBox(doc.blocks[0].table_rows[0].cells[0].bbox, expected);
    expectBox(doc.blocks[0].source_refs[0].bbox, expected);
    expectBox(doc.blocks[0].source_refs[1].bbox, other_expected);
    expectBox(doc.blocks[0].table_rows[0].cells[0].source_refs[1].bbox, other_expected);
    EXPECT_EQ(doc.pages[1].width, 100);
    EXPECT_EQ(doc.pages[1].height, 60);
    EXPECT_EQ(artifacts.pages[1].image.width, 100);
    EXPECT_EQ(artifacts.pages[1].image.height, 60);
}
} // namespace
