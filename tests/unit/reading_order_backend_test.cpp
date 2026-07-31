#include "reading_order/reading_order_backend.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

doc_parser::document::PageArtifact makePage() {
    doc_parser::document::PageArtifact page;
    page.page_index = 0;
    page.page_number = 1;
    page.width = 1000;
    page.height = 1400;
    return page;
}

doc_parser::document::LayoutBlock
makeBlock(const std::string& id, doc_parser::document::LayoutBlockType type, doc_parser::document::BBox bbox) {
    doc_parser::document::LayoutBlock block;
    block.id = id;
    block.type = type;
    block.bbox = bbox;
    block.confidence = 0.9;
    return block;
}

doc_parser::document::PageLayout makeLayout(std::vector<doc_parser::document::LayoutBlock> blocks) {
    doc_parser::document::PageLayout layout;
    layout.page_index = 0;
    layout.page_number = 1;
    layout.blocks = std::move(blocks);
    return layout;
}

std::vector<std::string> orderedIds(const doc_parser::document::PageReadingOrder& order) {
    std::vector<std::string> ids;
    for (const auto& item : order.items) {
        ids.push_back(item.layout_block_id);
    }
    return ids;
}

doc_parser::document::PageReadingOrder orderLayout(const doc_parser::document::PageLayout& layout) {
    const doc_parser::reading_order::DoclingLikeReadingOrderBackend backend;
    doc_parser::reading_order::ReadingOrderResult result;
    EXPECT_TRUE(backend.order({makePage(), layout}, result));
    return result.reading_order;
}

} // namespace

TEST(ReadingOrderBackendTest, OrdersMultiColumnBlocksTopToBottomPerColumn) {
    const auto layout = makeLayout({
        makeBlock("right_top", doc_parser::document::LayoutBlockType::Text, {600.0, 100.0, 900.0, 150.0}),
        makeBlock("left_bottom", doc_parser::document::LayoutBlockType::Text, {100.0, 180.0, 400.0, 230.0}),
        makeBlock("right_bottom", doc_parser::document::LayoutBlockType::Text, {600.0, 180.0, 900.0, 230.0}),
        makeBlock("left_top", doc_parser::document::LayoutBlockType::Text, {100.0, 100.0, 400.0, 150.0}),
    });

    const doc_parser::reading_order::DoclingLikeReadingOrderBackend backend;
    doc_parser::reading_order::ReadingOrderResult result;
    ASSERT_TRUE(backend.order({makePage(), layout}, result));
    const doc_parser::document::PageReadingOrder& order = result.reading_order;

    ASSERT_EQ(order.items.size(), 4U);
    EXPECT_EQ(order.items[0].layout_block_id, "left_top");
    EXPECT_EQ(order.items[1].layout_block_id, "left_bottom");
    EXPECT_EQ(order.items[2].layout_block_id, "right_top");
    EXPECT_EQ(order.items[3].layout_block_id, "right_bottom");
}

TEST(ReadingOrderBackendTest, KeepsHeadersBeforeBodyAndFootersAfterBody) {
    const auto layout = makeLayout({
        makeBlock("footer", doc_parser::document::LayoutBlockType::Footer, {100.0, 1260.0, 900.0, 1300.0}),
        makeBlock("body", doc_parser::document::LayoutBlockType::Text, {100.0, 300.0, 900.0, 360.0}),
        makeBlock("header", doc_parser::document::LayoutBlockType::Header, {100.0, 40.0, 900.0, 80.0}),
    });

    const doc_parser::reading_order::DoclingLikeReadingOrderBackend backend;
    doc_parser::reading_order::ReadingOrderResult result;
    ASSERT_TRUE(backend.order({makePage(), layout}, result));
    const doc_parser::document::PageReadingOrder& order = result.reading_order;

    ASSERT_EQ(order.items.size(), 3U);
    EXPECT_EQ(order.items[0].layout_block_id, "header");
    EXPECT_EQ(order.items[1].layout_block_id, "body");
    EXPECT_EQ(order.items[2].layout_block_id, "footer");
}

TEST(ReadingOrderBackendTest, OrdersUnequalFooterBlocksFromLeftToRight) {
    const auto layout = makeLayout({
        makeBlock("right_footer", doc_parser::document::LayoutBlockType::Footer, {600.0, 1260.0, 950.0, 1300.0}),
        makeBlock("body", doc_parser::document::LayoutBlockType::Text, {100.0, 300.0, 900.0, 360.0}),
        makeBlock("left_footer", doc_parser::document::LayoutBlockType::Footer, {50.0, 1265.0, 150.0, 1300.0}),
    });

    EXPECT_EQ(orderedIds(orderLayout(layout)), (std::vector<std::string>{"body", "left_footer", "right_footer"}));
}

TEST(ReadingOrderBackendTest, FinishesBothColumnsBeforeFollowingSpanningBlock) {
    const auto layout = makeLayout({
        makeBlock("right_top", doc_parser::document::LayoutBlockType::Text, {600.0, 180.0, 900.0, 230.0}),
        makeBlock("bottom", doc_parser::document::LayoutBlockType::Text, {100.0, 500.0, 900.0, 550.0}),
        makeBlock("left_bottom", doc_parser::document::LayoutBlockType::Text, {100.0, 280.0, 400.0, 330.0}),
        makeBlock("title", doc_parser::document::LayoutBlockType::Title, {100.0, 80.0, 900.0, 130.0}),
        makeBlock("right_bottom", doc_parser::document::LayoutBlockType::Text, {600.0, 280.0, 900.0, 330.0}),
        makeBlock("left_top", doc_parser::document::LayoutBlockType::Text, {100.0, 180.0, 400.0, 230.0}),
    });

    const doc_parser::reading_order::DoclingLikeReadingOrderBackend backend;
    doc_parser::reading_order::ReadingOrderResult result;
    ASSERT_TRUE(backend.order({makePage(), layout}, result));
    const auto& items = result.reading_order.items;

    ASSERT_EQ(items.size(), 6U);
    EXPECT_EQ(items[0].layout_block_id, "title");
    EXPECT_EQ(items[1].layout_block_id, "left_top");
    EXPECT_EQ(items[2].layout_block_id, "left_bottom");
    EXPECT_EQ(items[3].layout_block_id, "right_top");
    EXPECT_EQ(items[4].layout_block_id, "right_bottom");
    EXPECT_EQ(items[5].layout_block_id, "bottom");
}

TEST(ReadingOrderBackendTest, PlacesLinkedCaptionAfterTarget) {
    auto figure = makeBlock("figure", doc_parser::document::LayoutBlockType::Figure, {100.0, 200.0, 500.0, 500.0});
    figure.source_label = "Picture";
    auto caption = makeBlock("caption", doc_parser::document::LayoutBlockType::Text, {100.0, 100.0, 500.0, 140.0});
    caption.source_label = "Caption";
    caption.related_block_id = "figure";
    const auto layout = makeLayout({caption, figure});

    const doc_parser::reading_order::DoclingLikeReadingOrderBackend backend;
    doc_parser::reading_order::ReadingOrderResult result;
    ASSERT_TRUE(backend.order({makePage(), layout}, result));

    ASSERT_EQ(result.reading_order.items.size(), 2U);
    EXPECT_EQ(result.reading_order.items[0].layout_block_id, "figure");
    EXPECT_EQ(result.reading_order.items[1].layout_block_id, "caption");
}

TEST(ReadingOrderBackendTest, DetectsThreeColumnsIndependentlyOfInputOrder) {
    const auto first = makeLayout({
        makeBlock("middle_bottom", doc_parser::document::LayoutBlockType::Text, {380.0, 220.0, 620.0, 270.0}),
        makeBlock("right_top", doc_parser::document::LayoutBlockType::Text, {680.0, 100.0, 920.0, 150.0}),
        makeBlock("left_bottom", doc_parser::document::LayoutBlockType::Text, {80.0, 220.0, 320.0, 270.0}),
        makeBlock("middle_top", doc_parser::document::LayoutBlockType::Text, {380.0, 100.0, 620.0, 150.0}),
        makeBlock("right_bottom", doc_parser::document::LayoutBlockType::Text, {680.0, 220.0, 920.0, 270.0}),
        makeBlock("left_top", doc_parser::document::LayoutBlockType::Text, {80.0, 100.0, 320.0, 150.0}),
    });
    const auto second = makeLayout({
        first.blocks[5],
        first.blocks[3],
        first.blocks[1],
        first.blocks[2],
        first.blocks[0],
        first.blocks[4],
    });
    const std::vector<std::string> expected{
        "left_top", "left_bottom", "middle_top", "middle_bottom", "right_top", "right_bottom"};

    const auto first_order = orderLayout(first);
    const auto second_order = orderLayout(second);

    EXPECT_EQ(orderedIds(first_order), expected);
    EXPECT_EQ(orderedIds(second_order), expected);
    EXPECT_EQ(first_order.trace.algorithm, "band-column-topological-v2");
    ASSERT_EQ(first_order.trace.placements.size(), 6U);
    EXPECT_EQ(first_order.trace.placements[0].column_end, 1);
    EXPECT_EQ(first_order.trace.placements[1].column_end, 2);
}

TEST(ReadingOrderBackendTest, PreservesASeparatedSingleBlockColumn) {
    const auto layout = makeLayout({
        makeBlock("right_bottom", doc_parser::document::LayoutBlockType::Text, {680.0, 400.0, 920.0, 680.0}),
        makeBlock("middle_top", doc_parser::document::LayoutBlockType::Text, {380.0, 100.0, 620.0, 380.0}),
        makeBlock("left", doc_parser::document::LayoutBlockType::Text, {80.0, 100.0, 320.0, 500.0}),
        makeBlock("right_top", doc_parser::document::LayoutBlockType::Text, {680.0, 100.0, 920.0, 380.0}),
        makeBlock("middle_bottom", doc_parser::document::LayoutBlockType::Text, {380.0, 400.0, 620.0, 680.0}),
    });

    const auto order = orderLayout(layout);

    EXPECT_EQ(orderedIds(order),
              (std::vector<std::string>{"left", "middle_top", "middle_bottom", "right_top", "right_bottom"}));
    ASSERT_EQ(order.trace.placements.size(), 5U);
    EXPECT_EQ(order.trace.placements[2].column_start, 0);
    EXPECT_EQ(order.trace.placements[0].column_start, 2);
}

TEST(ReadingOrderBackendTest, DetectsColumnsWhenParagraphBlocksAreTall) {
    const auto layout = makeLayout({
        makeBlock("right", doc_parser::document::LayoutBlockType::Text, {520.0, 100.0, 920.0, 900.0}),
        makeBlock("left", doc_parser::document::LayoutBlockType::Text, {80.0, 100.0, 480.0, 900.0}),
    });

    const auto order = orderLayout(layout);

    EXPECT_EQ(orderedIds(order), (std::vector<std::string>{"left", "right"}));
    ASSERT_EQ(order.trace.placements.size(), 2U);
    EXPECT_NE(order.trace.placements[0].column_start, order.trace.placements[1].column_start);
}

TEST(ReadingOrderBackendTest, UsesContentColumnsForNarrowSpanningSeparators) {
    const auto layout = makeLayout({
        makeBlock("right", doc_parser::document::LayoutBlockType::Text, {530.0, 180.0, 700.0, 260.0}),
        makeBlock("bottom", doc_parser::document::LayoutBlockType::Text, {300.0, 500.0, 700.0, 550.0}),
        makeBlock("left", doc_parser::document::LayoutBlockType::Text, {300.0, 180.0, 470.0, 260.0}),
        makeBlock("title", doc_parser::document::LayoutBlockType::Title, {300.0, 80.0, 700.0, 130.0}),
    });

    EXPECT_EQ(orderedIds(orderLayout(layout)), (std::vector<std::string>{"title", "left", "right", "bottom"}));
}

TEST(ReadingOrderBackendTest, DoesNotLetCenteredMetadataBridgeTwoColumns) {
    const auto layout = makeLayout({
        makeBlock("right_bottom", doc_parser::document::LayoutBlockType::Text, {550.0, 280.0, 920.0, 360.0}),
        makeBlock("metadata", doc_parser::document::LayoutBlockType::Text, {400.0, 110.0, 600.0, 150.0}),
        makeBlock("left_bottom", doc_parser::document::LayoutBlockType::Text, {80.0, 280.0, 450.0, 360.0}),
        makeBlock("title", doc_parser::document::LayoutBlockType::Title, {80.0, 40.0, 920.0, 90.0}),
        makeBlock("right_top", doc_parser::document::LayoutBlockType::Text, {550.0, 180.0, 920.0, 260.0}),
        makeBlock("left_top", doc_parser::document::LayoutBlockType::Text, {80.0, 180.0, 450.0, 260.0}),
    });

    const auto order = orderLayout(layout);

    EXPECT_EQ(orderedIds(order),
              (std::vector<std::string>{"title", "metadata", "left_top", "left_bottom", "right_top", "right_bottom"}));
    const auto metadata = std::find_if(order.trace.placements.begin(),
                                       order.trace.placements.end(),
                                       [](const auto& placement) { return placement.layout_block_id == "metadata"; });
    ASSERT_NE(metadata, order.trace.placements.end());
    EXPECT_EQ(metadata->column_start, 0);
    EXPECT_EQ(metadata->column_end, 1);
}

TEST(ReadingOrderBackendTest, UsesTallTextInsteadOfNarrowHeadingsToEstimateColumnWidth) {
    const auto layout = makeLayout({
        makeBlock("right", doc_parser::document::LayoutBlockType::Text, {550.0, 100.0, 920.0, 900.0}),
        makeBlock("left_second", doc_parser::document::LayoutBlockType::Text, {80.0, 320.0, 480.0, 700.0}),
        makeBlock("narrow_heading", doc_parser::document::LayoutBlockType::Title, {80.0, 80.0, 220.0, 120.0}),
        makeBlock("left_first", doc_parser::document::LayoutBlockType::Text, {80.0, 140.0, 480.0, 300.0}),
        makeBlock("narrow_subheading", doc_parser::document::LayoutBlockType::Title, {80.0, 720.0, 250.0, 760.0}),
    });

    EXPECT_EQ(orderedIds(orderLayout(layout)),
              (std::vector<std::string>{"narrow_heading", "left_first", "left_second", "narrow_subheading", "right"}));
}

TEST(ReadingOrderBackendTest, DoesNotLetWideTablesHideNarrowGutters) {
    const auto layout = makeLayout({
        makeBlock("right", doc_parser::document::LayoutBlockType::Text, {510.0, 560.0, 920.0, 820.0}),
        makeBlock("table", doc_parser::document::LayoutBlockType::Table, {80.0, 120.0, 920.0, 500.0}),
        makeBlock("left", doc_parser::document::LayoutBlockType::Text, {80.0, 560.0, 490.0, 820.0}),
        makeBlock("title", doc_parser::document::LayoutBlockType::Title, {80.0, 40.0, 920.0, 90.0}),
    });

    EXPECT_EQ(orderedIds(orderLayout(layout)), (std::vector<std::string>{"title", "table", "left", "right"}));
}

TEST(ReadingOrderBackendTest, TreatsModelHintsAsSoftConstraints) {
    auto top = makeBlock("top", doc_parser::document::LayoutBlockType::Text, {100.0, 100.0, 500.0, 150.0});
    auto bottom = makeBlock("bottom", doc_parser::document::LayoutBlockType::Text, {100.0, 200.0, 500.0, 250.0});
    top.reading_order_hint = 10;
    bottom.reading_order_hint = 1;

    const auto order = orderLayout(makeLayout({bottom, top}));

    EXPECT_EQ(orderedIds(order), (std::vector<std::string>{"top", "bottom"}));
    ASSERT_EQ(order.trace.cycle_breaks.size(), 1U);
    EXPECT_EQ(order.trace.cycle_breaks[0].reason, "model_hint");
    EXPECT_DOUBLE_EQ(order.trace.cycle_breaks[0].confidence, 0.25);
}

TEST(ReadingOrderBackendTest, UsesModelHintsToResolveAmbiguousGeometry) {
    auto later = makeBlock("later", doc_parser::document::LayoutBlockType::Text, {100.0, 100.0, 500.0, 150.0});
    auto earlier = makeBlock("earlier", doc_parser::document::LayoutBlockType::Text, {100.0, 100.0, 500.0, 150.0});
    later.reading_order_hint = 20;
    earlier.reading_order_hint = 10;

    const auto order = orderLayout(makeLayout({later, earlier}));

    EXPECT_EQ(orderedIds(order), (std::vector<std::string>{"earlier", "later"}));
    EXPECT_TRUE(order.trace.cycle_breaks.empty());
    EXPECT_EQ(order.trace.edge_counts.at("model_hint"), 1);
}

TEST(ReadingOrderBackendTest, OrdersPartialSpanningContentAfterCoveredColumns) {
    const auto layout = makeLayout({
        makeBlock("span_right", doc_parser::document::LayoutBlockType::Figure, {380.0, 300.0, 920.0, 500.0}),
        makeBlock("right_top", doc_parser::document::LayoutBlockType::Text, {680.0, 100.0, 920.0, 160.0}),
        makeBlock("left_bottom", doc_parser::document::LayoutBlockType::Text, {80.0, 220.0, 320.0, 280.0}),
        makeBlock("middle_top", doc_parser::document::LayoutBlockType::Text, {380.0, 100.0, 620.0, 160.0}),
        makeBlock("left_top", doc_parser::document::LayoutBlockType::Text, {80.0, 100.0, 320.0, 160.0}),
    });

    EXPECT_EQ(orderedIds(orderLayout(layout)),
              (std::vector<std::string>{"left_top", "left_bottom", "middle_top", "right_top", "span_right"}));
}
