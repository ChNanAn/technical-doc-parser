#include "common/file_fingerprint.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

TEST(FileFingerprintTest, ComputesStandardSha256AndByteSize) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "tdp_file_fingerprint_standard_vector."
                                                                                "txt";
    {
        std::ofstream output(path, std::ios::binary);
        output << "abc";
    }

    doc_parser::common::FileFingerprint fingerprint;
    const doc_parser::common::Status status = doc_parser::common::fingerprintFile(path, fingerprint);

    ASSERT_TRUE(status.okStatus()) << status.message();
    EXPECT_EQ(fingerprint.size_bytes, 3U);
    EXPECT_EQ(fingerprint.sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::filesystem::remove(path);
}

TEST(FileFingerprintTest, ReportsTheConcreteUnreadableSource) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "tdp_file_fingerprint_missing_source."
                                                                                "txt";
    std::filesystem::remove(path);
    doc_parser::common::FileFingerprint fingerprint{42, "stale"};

    const doc_parser::common::Status status = doc_parser::common::fingerprintFile(path, fingerprint);

    EXPECT_FALSE(status.okStatus());
    EXPECT_EQ(status.code(), "source.fingerprint_open_failed");
    EXPECT_NE(status.message().find(path.string()), std::string::npos);
    EXPECT_EQ(fingerprint.size_bytes, 0U);
    EXPECT_TRUE(fingerprint.sha256.empty());
}
