#include "document/page_rotation.h"
#include "image/oriented_page_view.h"
#include "image/page_image_cache.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace {
using doc_parser::document::PageRotation;
using doc_parser::image::OrientedPageView;

class OrientedPageViewTest : public testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               ("tdp_view_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(root));
        pixels = cv::Mat(60, 100, CV_8UC3, cv::Scalar(250, 200, 100));
        pixels(cv::Rect(5, 12, 23, 8)).setTo(cv::Scalar(10, 50, 70));
        page.output_path = root / "source.png";
        page.width = pixels.cols;
        page.height = pixels.rows;
        ASSERT_TRUE(cv::imwrite(page.output_path.string(), pixels));
    }
    void TearDown() override { std::filesystem::remove_all(root); }
    std::filesystem::path root;
    doc_parser::document::PageArtifact page;
    cv::Mat pixels;
};

TEST_F(OrientedPageViewTest, SharesOnePoseWithFileAndCacheConsumersWithoutChangingSource) {
    for (const auto budget : {0U, 100000U}) {
        auto cache = std::make_shared<doc_parser::image::PageImageCache>(budget);
        page.image_cache = cache;
        const auto shared_source = cache->read(page.output_path);
        for (int degrees : {90, 180, 270}) {
            std::filesystem::path temporary;
            {
                OrientedPageView view;
                ASSERT_TRUE(view.prepare(page, PageRotation(100, 60, degrees), root));
                temporary = view.page().output_path;
                cv::Mat expected;
                cv::rotate(pixels,
                           expected,
                           degrees == 90    ? cv::ROTATE_90_CLOCKWISE
                           : degrees == 180 ? cv::ROTATE_180
                                            : cv::ROTATE_90_COUNTERCLOCKWISE);
                EXPECT_EQ(view.page().width, expected.cols);
                EXPECT_EQ(view.page().height, expected.rows);
                EXPECT_EQ(cv::norm(cv::imread(temporary.string()), expected, cv::NORM_INF), 0);
                EXPECT_EQ(cv::norm(doc_parser::image::readPageImage(view.page()), expected, cv::NORM_INF), 0);
                EXPECT_EQ(cv::norm(shared_source, pixels, cv::NORM_INF), 0);
            }
            EXPECT_FALSE(std::filesystem::exists(temporary.parent_path()));
        }
        EXPECT_LE(cache->stats().resident_bytes, budget);
        EXPECT_EQ(cv::norm(cv::imread(page.output_path.string()), pixels, cv::NORM_INF), 0);
    }
}

TEST_F(OrientedPageViewTest, CleansPrivateFilesWhenConsumersThrow) {
    std::filesystem::path temporary;
    EXPECT_THROW(
        {
            OrientedPageView view;
            ASSERT_TRUE(view.prepare(page, PageRotation(100, 60, 180), root));
            temporary = view.page().output_path;
            throw std::runtime_error("consumer failed");
        },
        std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(temporary.parent_path()));
    EXPECT_TRUE(std::filesystem::exists(page.output_path));
}

TEST_F(OrientedPageViewTest, IdentityDoesNoImageWorkAndPreparationFailuresPreserveSource) {
    OrientedPageView identity;
    auto missing = page;
    missing.output_path = root / "missing.png";
    ASSERT_TRUE(identity.prepare(missing, PageRotation(100, 60, 0), root));
    EXPECT_EQ(identity.page().output_path, missing.output_path);
    OrientedPageView wrong_size;
    EXPECT_FALSE(wrong_size.prepare(page, PageRotation(101, 60, 180), root));
    OrientedPageView invalid_root;
    EXPECT_FALSE(invalid_root.prepare(page, PageRotation(100, 60, 180), page.output_path));
    EXPECT_EQ(cv::norm(cv::imread(page.output_path.string()), pixels, cv::NORM_INF), 0);
}
} // namespace
