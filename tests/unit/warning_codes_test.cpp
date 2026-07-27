#include "common/warning_codes.h"

#include <gtest/gtest.h>

#include <set>
#include <string_view>

TEST(WarningCodeRegistryTest, DefinitionsAreUniqueAndComplete) {
    std::set<std::string_view> codes;
    for (const doc_parser::common::warning_codes::Definition& definition :
         doc_parser::common::warning_codes::kRegistry) {
        EXPECT_FALSE(definition.code.empty());
        EXPECT_FALSE(definition.stage.empty());
        EXPECT_FALSE(definition.summary.empty());
        EXPECT_TRUE(codes.insert(definition.code).second);
        EXPECT_TRUE(doc_parser::common::warning_codes::isRegistered(definition.code));
    }

    EXPECT_EQ(codes.size(), 3U);
    EXPECT_TRUE(codes.count(doc_parser::common::warning_codes::kOcrEnhancementFailed));
    EXPECT_TRUE(codes.count(doc_parser::common::warning_codes::kLayoutBackendFallback));
    EXPECT_TRUE(codes.count(doc_parser::common::warning_codes::kTableBackendFallback));
    EXPECT_FALSE(doc_parser::common::warning_codes::isRegistered("EXTENSION_WARNING"));
}
