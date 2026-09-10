#include "image/oriented_page_view.h"

#include "image/page_image_cache.h"

#include <atomic>
#include <chrono>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <system_error>

namespace doc_parser::image {

OrientedPageView::~OrientedPageView() {
    if (!directory_.empty()) {
        std::error_code ignored;
        std::filesystem::remove(directory_ / "page.png", ignored);
        std::filesystem::remove(directory_, ignored);
    }
}

bool OrientedPageView::prepare(const document::PageArtifact& source,
                               const document::PageRotation& rotation,
                               const std::filesystem::path& work_root) {
    if (!directory_.empty())
        return false;
    page_ = source;
    if (rotation.degrees() == 0)
        return true;
    try {
        const auto input = readPageImage(source);
        if (input.empty() || input.cols != rotation.sourceWidth() || input.rows != rotation.sourceHeight())
            return false;
        cv::Mat corrected;
        const int code = rotation.degrees() == 90    ? cv::ROTATE_90_CLOCKWISE
                         : rotation.degrees() == 180 ? cv::ROTATE_180
                                                     : cv::ROTATE_90_COUNTERCLOCKWISE;
        cv::rotate(input, corrected, code);
        static std::atomic<unsigned long long> sequence{0};
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto candidate =
                work_root / (".die-orientation-" + std::to_string(tick) + "-" + std::to_string(sequence.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                directory_ = candidate;
                break;
            }
            if (error && error != std::errc::file_exists)
                return false;
        }
        if (directory_.empty())
            return false;
        page_.output_path = directory_ / "page.png";
        page_.width = rotation.workingWidth();
        page_.height = rotation.workingHeight();
        return cv::imwrite(page_.output_path.string(), corrected);
    } catch (const cv::Exception&) {
        return false;
    }
}
} // namespace doc_parser::image
