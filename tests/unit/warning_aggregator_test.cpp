#include "document/warning_aggregator.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

doc_parser::document::DocumentWarning fallbackWarning(std::string page_id, std::string fallback_backend = "text") {
    return {
        "LAYOUT_BACKEND_FALLBACK",
        "layout inference failed; continued with the configured fallback chain",
        "layout",
        std::move(page_id),
        {},
        {
            {"failed_backend", "doclaynet"},
            {"fallback_backend", std::move(fallback_backend)},
            {"reason", "inference_failed"},
        },
    };
}

} // namespace

TEST(WarningAggregatorTest, MergesEquivalentWarningsAndPreservesEveryAffectedPage) {
    const std::vector<doc_parser::document::DocumentWarning> warnings{
        fallbackWarning("page_1"),
        fallbackWarning("page_2"),
        fallbackWarning("page_2"),
        fallbackWarning("page_3"),
    };

    const auto aggregated = doc_parser::document::aggregateWarnings(warnings);

    ASSERT_EQ(aggregated.size(), 1U);
    EXPECT_EQ(aggregated[0].occurrence_count, 4U);
    EXPECT_TRUE(aggregated[0].page_id.empty());
    EXPECT_EQ(aggregated[0].page_ids, (std::vector<std::string>{"page_1", "page_2", "page_3"}));
}

TEST(WarningAggregatorTest, KeepsDifferentMachineReadableDetailsSeparate) {
    const std::vector<doc_parser::document::DocumentWarning> warnings{
        fallbackWarning("page_1", "text"),
        fallbackWarning("page_2", "paddle-layout"),
    };

    const auto aggregated = doc_parser::document::aggregateWarnings(warnings);

    ASSERT_EQ(aggregated.size(), 2U);
    EXPECT_EQ(aggregated[0].page_id, "page_1");
    EXPECT_EQ(aggregated[1].page_id, "page_2");
    EXPECT_EQ(aggregated[0].occurrence_count, 1U);
    EXPECT_EQ(aggregated[1].occurrence_count, 1U);
}
