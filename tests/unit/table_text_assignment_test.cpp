#include "table/table_text_assignment.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using doc_parser::document::BBox;
using doc_parser::document::Table;
using doc_parser::document::TableCell;
using doc_parser::table::detail::assignTableText;
using doc_parser::table::detail::TableTextToken;

TableCell makeCell(int row, int column, BBox bbox) {
    TableCell cell;
    cell.row_index = row;
    cell.column_index = column;
    cell.bbox = bbox;
    cell.confidence = 0.95;
    return cell;
}

Table makeTable(const std::vector<TableCell>& cells) {
    Table table;
    table.bbox = {0.0, 0.0, 200.0, 200.0};
    table.rows.resize(1);
    table.rows.front().cells = cells;
    return table;
}

std::map<std::pair<int, int>, std::string> cellTexts(const Table& table) {
    std::map<std::pair<int, int>, std::string> result;
    for (const auto& row : table.rows) {
        for (const auto& cell : row.cells) {
            result[{cell.row_index, cell.column_index}] = cell.text;
        }
    }
    return result;
}

} // namespace

TEST(TableTextAssignmentTest, AssignsOverlappingRowsOnceUsingCoverageBeforeProximity) {
    auto table = makeTable({makeCell(0, 0, {0, 0, 100, 55}), makeCell(1, 0, {0, 40, 100, 60})});
    const auto stats = assignTableText(table, {{"shared", {10, 35, 90, 55}, 0.8}});
    EXPECT_EQ(table.rows[0].cells[0].text, "shared");
    EXPECT_TRUE(table.rows[0].cells[1].text.empty());
    EXPECT_DOUBLE_EQ(table.rows[0].cells[0].confidence, 0.8);
    EXPECT_DOUBLE_EQ(table.rows[0].cells[1].confidence, 0.95);
    EXPECT_EQ(stats.assigned_tokens, 1U);
    EXPECT_EQ(stats.ambiguous_tokens, 1U);
    EXPECT_EQ(stats.unassigned_tokens, 0U);
}

TEST(TableTextAssignmentTest, UsesNearestCellForFullyCoveredToken) {
    auto table = makeTable({makeCell(0, 0, {0, 0, 100, 60}), makeCell(1, 0, {0, 30, 100, 90})});
    assignTableText(table, {{"near second", {40, 50, 60, 58}, 1.0}});
    EXPECT_TRUE(table.rows[0].cells[0].text.empty());
    EXPECT_EQ(table.rows[0].cells[1].text, "near second");
}

TEST(TableTextAssignmentTest, ResolvesSharedBoundariesIndependentlyOfCellStorageOrder) {
    std::vector<TableCell> cells{makeCell(0, 0, {0, 0, 100, 50}),
                                 makeCell(0, 1, {100, 0, 200, 50}),
                                 makeCell(1, 0, {0, 50, 100, 100}),
                                 makeCell(1, 1, {100, 50, 200, 100})};
    const std::map<std::pair<int, int>, std::string> expected{
        {{0, 0}, "junction"}, {{0, 1}, ""}, {{1, 0}, ""}, {{1, 1}, ""}};
    do {
        auto table = makeTable(cells);
        const auto stats = assignTableText(table, {{"junction", {95, 45, 105, 55}, 1.0}});
        EXPECT_EQ(cellTexts(table), expected);
        EXPECT_EQ(stats.assigned_tokens, 1U);
        EXPECT_EQ(stats.ambiguous_tokens, 1U);
    } while (std::next_permutation(cells.begin(), cells.end(), [](const auto& lhs, const auto& rhs) {
        return std::make_pair(lhs.row_index, lhs.column_index) < std::make_pair(rhs.row_index, rhs.column_index);
    }));
}

TEST(TableTextAssignmentTest, PreservesMergedCellMetadataAndSourceCoordinates) {
    auto merged = makeCell(0, 0, {0, 0, 200, 80});
    merged.row_span = 2;
    merged.column_span = 2;
    merged.is_header = true;
    merged.source_refs.push_back({"page_1", merged.bbox, "source", doc_parser::document::TextSource::PdfTextLayer});
    auto table = makeTable({merged, makeCell(1, 0, {0, 50, 100, 100})});
    assignTableText(table, {{"heading", {80, 35, 120, 65}, 0.9}});
    const auto& cell = table.rows[0].cells[0];
    EXPECT_EQ(cell.text, "heading");
    EXPECT_TRUE(table.rows[0].cells[1].text.empty());
    EXPECT_EQ(cell.row_span, 2);
    EXPECT_EQ(cell.column_span, 2);
    EXPECT_TRUE(cell.is_header);
    EXPECT_DOUBLE_EQ(cell.bbox.x1, 200);
    EXPECT_DOUBLE_EQ(cell.bbox.y1, 80);
    ASSERT_EQ(cell.source_refs.size(), 1U);
    EXPECT_EQ(cell.source_refs[0].text, "source");
    EXPECT_EQ(cell.source_refs[0].source, doc_parser::document::TextSource::PdfTextLayer);
}

TEST(TableTextAssignmentTest, KeepsIdenticalTextAtDistinctSourcePositions) {
    auto table = makeTable({makeCell(0, 0, {0, 0, 100, 50}), makeCell(1, 0, {0, 50, 100, 100})});
    const auto stats = assignTableText(table, {{"42", {10, 10, 20, 20}, 0.8}, {"42", {10, 60, 20, 70}, 0.6}});
    EXPECT_EQ(table.rows[0].cells[0].text, "42");
    EXPECT_EQ(table.rows[0].cells[1].text, "42");
    EXPECT_EQ(stats.assigned_tokens, 2U);
    EXPECT_EQ(stats.ambiguous_tokens, 0U);
}

TEST(TableTextAssignmentTest, ReportsGridGapsWithoutPullingInOutsideText) {
    auto table = makeTable({makeCell(0, 0, {0, 0, 100, 40}), makeCell(1, 0, {0, 60, 100, 100})});
    const auto stats = assignTableText(
        table,
        {{"gap", {10, 45, 30, 55}, 1.0}, {"caption", {10, 210, 100, 230}, 1.0}, {"body", {10, 10, 40, 20}, 1.0}});
    EXPECT_EQ(stats.unassigned_tokens, 1U);
    EXPECT_EQ(stats.assigned_tokens, 1U);
    EXPECT_EQ(table.rows[0].cells[0].text, "body");
    EXPECT_TRUE(table.rows[0].cells[1].text.empty());

    auto empty = makeTable({});
    EXPECT_EQ(assignTableText(empty, {{"unstructured", {10, 10, 40, 20}, 1.0}}).unassigned_tokens, 1U);
}

TEST(TableTextAssignmentTest, OrdersBoundedBaselinesConsistentlyAcrossTokenPermutations) {
    // The old pairwise tolerance would compare A < C, C < B, but B < A.
    const std::vector<TableTextToken> tokens{
        {"A", {90, 0, 100, 10}, 0.9}, {"B", {50, 2, 60, 12}, 0.8}, {"C", {10, 4, 20, 14}, 0.7}};
    std::vector<int> order{0, 1, 2};
    do {
        auto table = makeTable({makeCell(0, 0, {0, 0, 200, 100})});
        assignTableText(table, {tokens[order[0]], tokens[order[1]], tokens[order[2]]});
        EXPECT_EQ(table.rows[0].cells[0].text, "B A C");
        EXPECT_NEAR(table.rows[0].cells[0].confidence, 0.8, 1e-12);
    } while (std::next_permutation(order.begin(), order.end()));
}

TEST(TableTextAssignmentTest, IgnoresInvalidGeometryBeforeMatchingAndSorting) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    auto table = makeTable(
        {makeCell(0, 0, {0, 0, 100, 100}), makeCell(0, 1, {100, 0, 0, 100}), makeCell(0, 2, {0, 0, 100, inf})});
    const auto stats = assignTableText(table,
                                       {{"ok", {10, 10, 20, 20}, 1.0},
                                        {"nan", {nan, 10, 20, 20}, 1.0},
                                        {"inf", {10, 10, inf, 20}, 1.0},
                                        {"point", {10, 10, 10, 10}, 1.0},
                                        {"", {10, 10, 20, 20}, 1.0}});
    EXPECT_EQ(stats.assigned_tokens, 1U);
    EXPECT_EQ(stats.ambiguous_tokens, 0U);
    EXPECT_EQ(stats.unassigned_tokens, 0U);
    EXPECT_EQ(table.rows[0].cells[0].text, "ok");
    EXPECT_TRUE(table.rows[0].cells[1].text.empty());
    EXPECT_TRUE(table.rows[0].cells[2].text.empty());
}

TEST(TableTextAssignmentTest, CollectsSpansOrFallsBackToWholeLines) {
    doc_parser::document::PageText page;
    doc_parser::document::TextLine native;
    native.text = "native text";
    native.spans = {{"native", {10, 10, 40, 20}, doc_parser::document::TextSource::PdfTextLayer, 0.9},
                    {"", {}, doc_parser::document::TextSource::PdfTextLayer, 0.0},
                    {"text", {50, 10, 70, 20}, doc_parser::document::TextSource::PdfTextLayer, 0.7}};
    page.lines.push_back(native);
    page.lines.push_back({"OCR line", {10, 60, 80, 70}, doc_parser::document::TextSource::Ocr, 0.8, {}});
    page.lines.push_back({});
    const auto tokens = doc_parser::table::detail::collectTableTextTokens(page);
    ASSERT_EQ(tokens.size(), 3U);
    auto table = makeTable({makeCell(0, 0, {0, 0, 100, 100})});
    const auto stats = assignTableText(table, tokens);
    EXPECT_EQ(stats.assigned_tokens, 3U);
    EXPECT_EQ(table.rows[0].cells[0].text, "native text OCR line");
    EXPECT_NEAR(table.rows[0].cells[0].confidence, 0.8, 1e-12);
}
