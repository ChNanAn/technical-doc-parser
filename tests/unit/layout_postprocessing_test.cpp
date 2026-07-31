#include "layout/layout_postprocessing.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

doc_parser::document::TextLine makeLine(const std::string& text, doc_parser::document::BBox bbox) {
    doc_parser::document::TextLine line;
    line.text = text;
    line.bbox = bbox;
    return line;
}

doc_parser::document::LayoutBlock makeTextBlock(std::vector<int> line_indices) {
    doc_parser::document::LayoutBlock block;
    block.id = "merged";
    block.type = doc_parser::document::LayoutBlockType::Text;
    block.bbox = {80.0, 80.0, 920.0, 400.0};
    block.text_line_indices = std::move(line_indices);
    return block;
}

doc_parser::document::PageArtifact makePage() {
    doc_parser::document::PageArtifact page;
    page.width = 1000;
    page.height = 1000;
    return page;
}

} // namespace

TEST(LayoutPostprocessingTest, ReordersInterleavedLinesInsideMergedMultiColumnBlock) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("left one", {100.0, 100.0, 400.0, 130.0}),
        makeLine("right one", {600.0, 100.0, 900.0, 130.0}),
        makeLine("left two", {100.0, 150.0, 400.0, 180.0}),
        makeLine("right two", {600.0, 150.0, 900.0, 180.0}),
    };
    std::vector<doc_parser::document::LayoutBlock> blocks{makeTextBlock({0, 1, 2, 3})};

    const auto stats = doc_parser::layout::detail::refineMultiColumnTextLineOrder(text, blocks);

    EXPECT_EQ(blocks[0].text_line_indices, (std::vector<int>{0, 2, 1, 3}));
    EXPECT_EQ(stats.reordered_blocks, 1);
    EXPECT_EQ(stats.maximum_columns, 2);
}

TEST(LayoutPostprocessingTest, DoesNotTreatParagraphIndentationAsColumns) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("one", {100.0, 100.0, 500.0, 130.0}),
        makeLine("two", {150.0, 150.0, 500.0, 180.0}),
        makeLine("three", {100.0, 200.0, 500.0, 230.0}),
        makeLine("four", {150.0, 250.0, 500.0, 280.0}),
    };
    std::vector<doc_parser::document::LayoutBlock> blocks{makeTextBlock({0, 1, 2, 3})};

    const auto stats = doc_parser::layout::detail::refineMultiColumnTextLineOrder(text, blocks);

    EXPECT_EQ(blocks[0].text_line_indices, (std::vector<int>{0, 1, 2, 3}));
    EXPECT_EQ(stats.reordered_blocks, 0);
}

TEST(LayoutPostprocessingTest, RequiresColumnsToOverlapVertically) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("left one", {100.0, 100.0, 400.0, 130.0}),
        makeLine("left two", {100.0, 150.0, 400.0, 180.0}),
        makeLine("right one", {600.0, 300.0, 900.0, 330.0}),
        makeLine("right two", {600.0, 350.0, 900.0, 380.0}),
    };
    std::vector<doc_parser::document::LayoutBlock> blocks{makeTextBlock({0, 1, 2, 3})};

    const auto stats = doc_parser::layout::detail::refineMultiColumnTextLineOrder(text, blocks);

    EXPECT_EQ(blocks[0].text_line_indices, (std::vector<int>{0, 1, 2, 3}));
    EXPECT_EQ(stats.reordered_blocks, 0);
}

TEST(LayoutPostprocessingTest, RecoversValidUnassignedBodyLinesExactlyOnce) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("assigned", {100.0, 100.0, 400.0, 130.0}),
        makeLine("missing", {100.0, 500.0, 400.0, 530.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;
    layout.blocks = {makeTextBlock({0})};

    const auto first = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);
    const auto second = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 2U);
    EXPECT_EQ(layout.blocks[1].id, "page_1_fallback_line_2");
    EXPECT_EQ(layout.blocks[1].source_label, "text_line_fallback");
    EXPECT_EQ(layout.blocks[1].text_line_indices, (std::vector<int>{1}));
    EXPECT_EQ(first.recovered_lines, 1);
    EXPECT_EQ(second.recovered_lines, 0);
}

TEST(LayoutPostprocessingTest, DoesNotRecoverLinesCoveredByFurniture) {
    doc_parser::document::PageText text;
    text.lines = {makeLine("running header", {100.0, 20.0, 900.0, 50.0})};
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;
    auto header = makeTextBlock({});
    header.type = doc_parser::document::LayoutBlockType::Header;
    header.bbox = {80.0, 10.0, 920.0, 60.0};
    layout.blocks = {header};

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    EXPECT_EQ(layout.blocks.size(), 1U);
    EXPECT_EQ(stats.recovered_lines, 0);
    EXPECT_EQ(stats.skipped_furniture_lines, 1);
}

TEST(LayoutPostprocessingTest, GroupsAdjacentRecoveredLinesWithoutJoiningColumns) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("left one", {100.0, 100.0, 400.0, 130.0}),
        makeLine("right one", {600.0, 100.0, 900.0, 130.0}),
        makeLine("left two", {110.0, 140.0, 400.0, 170.0}),
        makeLine("right two", {600.0, 140.0, 900.0, 170.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 2U);
    EXPECT_EQ(layout.blocks[0].text_line_indices, (std::vector<int>{0, 2}));
    EXPECT_EQ(layout.blocks[1].text_line_indices, (std::vector<int>{1, 3}));
    EXPECT_EQ(stats.recovered_lines, 4);
    EXPECT_EQ(stats.fallback_blocks, 2);
}

TEST(LayoutPostprocessingTest, GroupsInlineListMarkerBeforeItsText) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("32.", {100.0, 100.0, 130.0, 130.0}),
        makeLine("reference entry", {135.0, 99.0, 500.0, 130.0}),
        makeLine("continuation", {135.0, 140.0, 500.0, 170.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 1U);
    EXPECT_EQ(layout.blocks[0].text_line_indices, (std::vector<int>{0, 1, 2}));
    EXPECT_EQ(stats.recovered_lines, 3);
    EXPECT_EQ(stats.fallback_blocks, 1);
}

TEST(LayoutPostprocessingTest, ClassifiesRecoveredPageEdgesAsFurniture) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("running header", {100.0, 20.0, 900.0, 50.0}),
        makeLine("body", {100.0, 300.0, 900.0, 330.0}),
        makeLine("42", {480.0, 940.0, 520.0, 970.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 3U);
    EXPECT_EQ(layout.blocks[0].type, doc_parser::document::LayoutBlockType::Text);
    EXPECT_EQ(layout.blocks[1].type, doc_parser::document::LayoutBlockType::Header);
    EXPECT_EQ(layout.blocks[1].source_label, "edge_header_fallback");
    EXPECT_EQ(layout.blocks[2].type, doc_parser::document::LayoutBlockType::Footer);
    EXPECT_EQ(layout.blocks[2].source_label, "edge_footer_fallback");
    EXPECT_EQ(stats.recovered_lines, 3);
    EXPECT_EQ(stats.recovered_furniture_lines, 2);
}

TEST(LayoutPostprocessingTest, ClassifiesUnconnectedMarginTextBeyondStrictPageEdgeAsFurniture) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("journal running title", {100.0, 80.0, 900.0, 110.0}),
        makeLine("body", {100.0, 300.0, 900.0, 330.0}),
        makeLine("doi: 10.1000/example", {100.0, 900.0, 500.0, 930.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 3U);
    EXPECT_EQ(layout.blocks[0].type, doc_parser::document::LayoutBlockType::Text);
    EXPECT_EQ(layout.blocks[1].type, doc_parser::document::LayoutBlockType::Header);
    EXPECT_EQ(layout.blocks[2].type, doc_parser::document::LayoutBlockType::Footer);
    EXPECT_EQ(stats.recovered_furniture_lines, 2);
}

TEST(LayoutPostprocessingTest, PreservesMarginTextThatContinuesAModelBackedBodyColumn) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("continued first line", {100.0, 60.0, 500.0, 90.0}),
        makeLine("model backed body", {100.0, 100.0, 500.0, 130.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;
    auto body = makeTextBlock({1});
    body.bbox = {90.0, 95.0, 510.0, 400.0};
    layout.blocks.push_back(body);

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 1U);
    EXPECT_EQ(layout.blocks[0].text_line_indices, (std::vector<int>{0, 1}));
    EXPECT_EQ(stats.preserved_edge_body_lines, 1);
    EXPECT_EQ(stats.attached_lines, 1);
    EXPECT_EQ(stats.recovered_furniture_lines, 0);
}

TEST(LayoutPostprocessingTest, PreservesLargeHeadingNearTopMargin) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("LARGE DOCUMENT TITLE", {100.0, 50.0, 900.0, 110.0}),
        makeLine("ordinary body one", {100.0, 300.0, 900.0, 330.0}),
        makeLine("ordinary body two", {100.0, 350.0, 900.0, 380.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 2U);
    EXPECT_EQ(layout.blocks[1].type, doc_parser::document::LayoutBlockType::Text);
    EXPECT_EQ(layout.blocks[1].source_label, "text_line_fallback");
    EXPECT_EQ(stats.preserved_edge_body_lines, 1);
}

TEST(LayoutPostprocessingTest, PreservesKickerAndHeadingNearTopMargin) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("SECTION", {100.0, 70.0, 260.0, 100.0}),
        makeLine("A descriptive section heading", {100.0, 105.0, 500.0, 135.0}),
        makeLine("ordinary body", {100.0, 300.0, 500.0, 330.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 2U);
    EXPECT_EQ(layout.blocks[1].type, doc_parser::document::LayoutBlockType::Text);
    EXPECT_EQ(layout.blocks[1].source_label, "text_line_fallback");
    EXPECT_EQ(stats.preserved_edge_body_lines, 2);
}

TEST(LayoutPostprocessingTest, ClassifiesUppercaseLetterheadNearTopMarginAsFurniture) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("DEPARTMENT OF ENGINEERING", {100.0, 80.0, 500.0, 105.0}),
        makeLine("EXAMPLE UNIVERSITY", {100.0, 110.0, 500.0, 135.0}),
        makeLine("EXAMPLE CITY 10001", {100.0, 140.0, 500.0, 165.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 1U);
    EXPECT_EQ(layout.blocks[0].type, doc_parser::document::LayoutBlockType::Header);
    EXPECT_EQ(layout.blocks[0].source_label, "edge_header_fallback");
    EXPECT_EQ(stats.recovered_furniture_lines, 3);
}

TEST(LayoutPostprocessingTest, DoesNotApplyKickerOverrideAtStrictPageEdge) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("SECTION", {100.0, 10.0, 260.0, 30.0}),
        makeLine("A running publication title", {100.0, 35.0, 500.0, 55.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 1U);
    EXPECT_EQ(layout.blocks[0].type, doc_parser::document::LayoutBlockType::Header);
    EXPECT_EQ(layout.blocks[0].source_label, "edge_header_fallback");
    EXPECT_EQ(stats.recovered_furniture_lines, 2);
}

TEST(LayoutPostprocessingTest, PreservesLongParagraphNearBottomMargin) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("model backed body", {100.0, 500.0, 500.0, 530.0}),
        makeLine("This paragraph contains substantial document content near the page edge.",
                 {100.0, 850.0, 700.0, 880.0}),
        makeLine("It continues across several lines because the layout model missed the region.",
                 {100.0, 885.0, 700.0, 915.0}),
        makeLine("The recovered text must remain available to downstream document users.",
                 {100.0, 920.0, 700.0, 950.0}),
        makeLine("Dropping the final line would make the extracted document incomplete.", {100.0, 955.0, 700.0, 985.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;
    auto body = makeTextBlock({0});
    body.bbox = {90.0, 490.0, 510.0, 540.0};
    layout.blocks.push_back(body);

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 2U);
    EXPECT_EQ(layout.blocks[1].type, doc_parser::document::LayoutBlockType::Text);
    EXPECT_EQ(layout.blocks[1].source_label, "text_line_fallback");
    EXPECT_EQ(stats.preserved_edge_body_lines, 4);
}

TEST(LayoutPostprocessingTest, ClassifiesLongCopyrightNoticeAsFurniture) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("Single-user license only.", {100.0, 880.0, 700.0, 900.0}),
        makeLine("Copying and network distribution are prohibited.", {100.0, 905.0, 700.0, 925.0}),
        makeLine("This material remains the property of the publisher.", {100.0, 930.0, 700.0, 950.0}),
        makeLine("Copyright Example Standards Organization.", {100.0, 955.0, 700.0, 975.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 1U);
    EXPECT_EQ(layout.blocks[0].type, doc_parser::document::LayoutBlockType::Footer);
    EXPECT_EQ(layout.blocks[0].source_label, "edge_footer_fallback");
    EXPECT_EQ(stats.recovered_furniture_lines, 4);
}

TEST(LayoutPostprocessingTest, PreservesFigureCaptionNearBottomMargin) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("Figure 4. Measured response for the treatment group.", {100.0, 880.0, 700.0, 910.0}),
        makeLine("Results are presented as mean values with confidence intervals.", {100.0, 915.0, 700.0, 945.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 1U);
    EXPECT_EQ(layout.blocks[0].type, doc_parser::document::LayoutBlockType::Text);
    EXPECT_EQ(layout.blocks[0].source_label, "text_line_fallback");
    EXPECT_EQ(stats.preserved_edge_body_lines, 2);
}

TEST(LayoutPostprocessingTest, DoesNotTreatOutOfBoundsHorizontalTextAsFurniture) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("text outside a malformed crop box", {1900.0, 300.0, 2500.0, 330.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 1U);
    EXPECT_EQ(layout.blocks[0].type, doc_parser::document::LayoutBlockType::Text);
    EXPECT_EQ(stats.recovered_furniture_lines, 0);
}

TEST(LayoutPostprocessingTest, SkipsRepeatedMonotonicMarginLineNumbers) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("10", {495.0, 150.0, 515.0, 170.0}),
        makeLine("15", {496.0, 300.0, 516.0, 320.0}),
        makeLine("20", {494.0, 450.0, 514.0, 470.0}),
        makeLine("30", {497.0, 600.0, 517.0, 620.0}),
        makeLine("35", {495.0, 750.0, 515.0, 770.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    EXPECT_TRUE(layout.blocks.empty());
    EXPECT_EQ(stats.recovered_lines, 0);
    EXPECT_EQ(stats.skipped_marginalia_lines, 5);
}

TEST(LayoutPostprocessingTest, RetainsIsolatedNumericPageMarkers) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("12", {495.0, 300.0, 515.0, 320.0}),
        makeLine("13", {495.0, 700.0, 515.0, 720.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 2U);
    EXPECT_EQ(stats.recovered_lines, 2);
    EXPECT_EQ(stats.skipped_marginalia_lines, 0);
}

TEST(LayoutPostprocessingTest, RetainsNonMonotonicNumericColumnContent) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("42", {495.0, 150.0, 515.0, 170.0}),
        makeLine("7", {495.0, 300.0, 515.0, 320.0}),
        makeLine("91", {495.0, 450.0, 515.0, 470.0}),
        makeLine("13", {495.0, 600.0, 515.0, 620.0}),
        makeLine("68", {495.0, 750.0, 515.0, 770.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    EXPECT_EQ(layout.blocks.size(), 5U);
    EXPECT_EQ(stats.recovered_lines, 5);
    EXPECT_EQ(stats.skipped_marginalia_lines, 0);
}

TEST(LayoutPostprocessingTest, CoalescesDenseAlignedGridAndKeepsRowWiseLineOrder) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("left one", {100.0, 200.0, 200.0, 220.0}),
        makeLine("left two", {100.0, 280.0, 200.0, 300.0}),
        makeLine("left three", {100.0, 360.0, 200.0, 380.0}),
        makeLine("middle one", {400.0, 200.0, 500.0, 220.0}),
        makeLine("middle two", {400.0, 280.0, 500.0, 300.0}),
        makeLine("middle three", {400.0, 360.0, 500.0, 380.0}),
        makeLine("right one", {700.0, 200.0, 800.0, 220.0}),
        makeLine("right two", {700.0, 280.0, 800.0, 300.0}),
        makeLine("right three", {700.0, 360.0, 800.0, 380.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 1U);
    EXPECT_EQ(layout.blocks[0].source_label, "text_grid_fallback");
    EXPECT_EQ(layout.blocks[0].text_line_indices, (std::vector<int>{0, 3, 6, 1, 4, 7, 2, 5, 8}));
    EXPECT_EQ(stats.recovered_lines, 9);
    EXPECT_EQ(stats.coalesced_grid_groups, 9);
    EXPECT_EQ(stats.fallback_blocks, 1);
}

TEST(LayoutPostprocessingTest, AttachesNearbyRecoveredBodyLineToCompatibleBlock) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("model-backed paragraph", {100.0, 200.0, 500.0, 230.0}),
        makeLine("recovered continuation", {100.0, 235.0, 500.0, 265.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;
    auto body = makeTextBlock({0});
    body.bbox = {90.0, 195.0, 510.0, 232.0};
    layout.blocks.push_back(body);

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 1U);
    EXPECT_EQ(layout.blocks[0].text_line_indices, (std::vector<int>{0, 1}));
    EXPECT_EQ(layout.blocks[0].bbox.y1, 265.0);
    EXPECT_EQ(stats.recovered_lines, 1);
    EXPECT_EQ(stats.attached_lines, 1);
    EXPECT_EQ(stats.attached_groups, 1);
    EXPECT_EQ(stats.fallback_blocks, 0);
}

TEST(LayoutPostprocessingTest, KeepsRecoveredCaptionStandaloneNearFigure) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("existing paragraph", {100.0, 455.0, 600.0, 485.0}),
        makeLine("Measured response for the treatment group", {100.0, 410.0, 600.0, 440.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;
    auto figure = makeTextBlock({});
    figure.type = doc_parser::document::LayoutBlockType::Figure;
    figure.bbox = {100.0, 200.0, 600.0, 400.0};
    auto body = makeTextBlock({0});
    body.bbox = {90.0, 450.0, 610.0, 490.0};
    layout.blocks = {figure, body};

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 3U);
    EXPECT_EQ(layout.blocks[2].source_label, "text_line_fallback");
    EXPECT_EQ(layout.blocks[2].text_line_indices, (std::vector<int>{1}));
    EXPECT_EQ(stats.attached_lines, 0);
}

TEST(LayoutPostprocessingTest, DoesNotAttachExplicitListItemToNearbyTitle) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("CHANGING THE BULB", {250.0, 200.0, 450.0, 230.0}),
        makeLine("1. Disconnect the power cord from the electrical outlet.", {250.0, 235.0, 520.0, 265.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;
    auto title = makeTextBlock({0});
    title.type = doc_parser::document::LayoutBlockType::Title;
    title.bbox = {245.0, 195.0, 455.0, 232.0};
    layout.blocks = {title};

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 2U);
    EXPECT_EQ(layout.blocks[0].text_line_indices, (std::vector<int>{0}));
    EXPECT_EQ(layout.blocks[1].text_line_indices, (std::vector<int>{1}));
    EXPECT_EQ(layout.blocks[1].source_label, "text_line_fallback");
    EXPECT_EQ(stats.attached_lines, 0);
}

TEST(LayoutPostprocessingTest, DoesNotAttachTallRecoveredParagraph) {
    doc_parser::document::PageText text;
    text.lines = {
        makeLine("first recovered line", {100.0, 200.0, 500.0, 230.0}),
        makeLine("second recovered line", {100.0, 235.0, 500.0, 265.0}),
        makeLine("third recovered line", {100.0, 270.0, 500.0, 300.0}),
        makeLine("model-backed paragraph", {100.0, 305.0, 500.0, 335.0}),
    };
    doc_parser::document::PageLayout layout;
    layout.page_number = 1;
    auto body = makeTextBlock({3});
    body.bbox = {90.0, 302.0, 510.0, 340.0};
    layout.blocks.push_back(body);

    const auto stats = doc_parser::layout::detail::recoverUnassignedTextLines(text, makePage(), layout);

    ASSERT_EQ(layout.blocks.size(), 2U);
    EXPECT_EQ(layout.blocks[1].source_label, "text_line_fallback");
    EXPECT_EQ(layout.blocks[1].text_line_indices, (std::vector<int>{0, 1, 2}));
    EXPECT_EQ(stats.attached_lines, 0);
    EXPECT_EQ(stats.fallback_blocks, 1);
}

TEST(LayoutPostprocessingTest, RefinesEligibleEdgeBlocksButKeepsBodyAndNonTextRegions) {
    auto very_top = makeTextBlock({0});
    very_top.bbox = {100.0, 10.0, 900.0, 40.0};
    auto top_title = makeTextBlock({1});
    top_title.type = doc_parser::document::LayoutBlockType::Title;
    top_title.bbox = {100.0, 40.0, 900.0, 80.0};
    auto first_content_band = makeTextBlock({2});
    first_content_band.bbox = {100.0, 60.0, 900.0, 100.0};
    auto body = makeTextBlock({3});
    body.bbox = {100.0, 200.0, 900.0, 800.0};
    auto bottom = makeTextBlock({4});
    bottom.bbox = {100.0, 930.0, 900.0, 970.0};
    auto top_figure = makeTextBlock({});
    top_figure.type = doc_parser::document::LayoutBlockType::Figure;
    top_figure.bbox = {100.0, 20.0, 900.0, 90.0};
    std::vector<doc_parser::document::LayoutBlock> blocks{
        very_top,
        top_title,
        first_content_band,
        body,
        bottom,
        top_figure,
    };

    const auto stats = doc_parser::layout::detail::refineEdgeFurniture(makePage(), blocks);

    EXPECT_EQ(blocks[0].type, doc_parser::document::LayoutBlockType::Header);
    EXPECT_EQ(blocks[1].type, doc_parser::document::LayoutBlockType::Title);
    EXPECT_EQ(blocks[2].type, doc_parser::document::LayoutBlockType::Text);
    EXPECT_EQ(blocks[3].type, doc_parser::document::LayoutBlockType::Text);
    EXPECT_EQ(blocks[4].type, doc_parser::document::LayoutBlockType::Footer);
    EXPECT_EQ(blocks[5].type, doc_parser::document::LayoutBlockType::Figure);
    EXPECT_EQ(stats.headers, 1);
    EXPECT_EQ(stats.footers, 1);
}
