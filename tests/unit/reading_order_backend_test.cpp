#include "reading_order/reading_order_backend.h"

#include <gtest/gtest.h>

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
