#include "common/atomic_output_file.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

class AtomicOutputFileTest : public testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               ("tdp_atomic_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(root));
        path = root / "document.json";
        std::ofstream(path) << "previous";
    }
    void TearDown() override { std::filesystem::remove_all(root); }
    std::string read() const {
        std::ifstream input(path);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
    std::filesystem::path root;
    std::filesystem::path path;
};

TEST_F(AtomicOutputFileTest, ReadersSeeOnlyCommittedOutput) {
    doc_parser::common::AtomicOutputFile file(path);
    ASSERT_TRUE(file.isOpen());
    file.stream() << "replacement" << std::flush;
    EXPECT_EQ(read(), "previous");
    ASSERT_TRUE(file.commit());
    EXPECT_EQ(read(), "replacement");
}

TEST_F(AtomicOutputFileTest, FailedWritesPreservePreviousOutputAndCleanTemporaryFiles) {
    {
        doc_parser::common::AtomicOutputFile file(path);
        file.stream() << "incomplete";
        file.stream().setstate(std::ios::badbit);
        EXPECT_FALSE(file.commit());
    }
    EXPECT_EQ(read(), "previous");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator()), 1);
}

TEST_F(AtomicOutputFileTest, WritersDoNotShareOrDeleteEachOthersTemporaryFile) {
    doc_parser::common::AtomicOutputFile first(path);
    doc_parser::common::AtomicOutputFile second(path);
    ASSERT_TRUE(first.isOpen());
    ASSERT_TRUE(second.isOpen());
    first.stream() << "first";
    second.stream() << "second";
    ASSERT_TRUE(first.commit());
    EXPECT_EQ(read(), "first");
    ASSERT_TRUE(second.commit());
    EXPECT_EQ(read(), "second");
}

TEST_F(AtomicOutputFileTest, SupportsLongDestinationNamesWithoutExtendingThemForTemporaryFiles) {
    path = root / (std::string(240, 'a') + ".json");
    doc_parser::common::AtomicOutputFile file(path);
    ASSERT_TRUE(file.isOpen());
    file.stream() << "complete";
    ASSERT_TRUE(file.commit());
    EXPECT_EQ(read(), "complete");
}

} // namespace
