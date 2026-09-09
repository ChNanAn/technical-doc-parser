#include "image/page_image_cache.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <utility>

namespace doc_parser::image {

PageImageCache::PageImageCache(std::size_t maximum_bytes) : maximum_bytes_(maximum_bytes) {}

bool PageImageCache::admitRendered(const document::PageArtifact& page, document::PageBitmap&& bitmap) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto reject = [&] {
        ++stats_.rendered_rejections;
        return false;
    };
    if (bitmap.width <= 0 || bitmap.height <= 0 || bitmap.channels != 4 || bitmap.width != page.width ||
        bitmap.height != page.height || bitmap.page_index != page.page_index ||
        bitmap.page_number != page.page_number) {
        return reject();
    }
    const auto width = static_cast<std::size_t>(bitmap.width);
    const auto height = static_cast<std::size_t>(bitmap.height);
    if (width > std::numeric_limits<std::size_t>::max() / 4 / height || bitmap.pixels.size() != width * height * 4) {
        return reject();
    }
    const auto bytes = bitmap.pixels.capacity();
    if (bytes > maximum_bytes_ - stats_.resident_bytes) {
        return reject();
    }
    const auto [entry, inserted] = images_.try_emplace(page.output_path.string());
    if (!inserted) {
        return reject();
    }
    entry->second.rendered = std::move(bitmap);
    stats_.resident_bytes += bytes;
    stats_.peak_resident_bytes = std::max(stats_.peak_resident_bytes, stats_.resident_bytes);
    ++stats_.rendered_admissions;
    return true;
}

cv::Mat PageImageCache::read(const std::filesystem::path& path) {
    const std::string key = path.string();
    // Serialize loading too, so simultaneous consumers cannot decode and admit
    // the same image twice.
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = images_.find(key);
    if (found != images_.end()) {
        auto& cached = found->second;
        if (cached.bgr.empty()) {
            auto& bitmap = cached.rendered;
            const auto started = std::chrono::steady_clock::now();
            const cv::Mat rgba(bitmap.height, bitmap.width, CV_8UC4, bitmap.pixels.data());
            cv::Mat bgr;
            cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
            stats_.conversion_microseconds += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started)
                    .count());
            ++stats_.conversions;
            cached.bgr = std::move(bgr);
            stats_.resident_bytes -= bitmap.pixels.capacity();
            stats_.resident_bytes += cached.bgr.total() * cached.bgr.elemSize();
            bitmap = {};
        }
        ++stats_.hits;
        return cached.bgr;
    }

    const auto started = std::chrono::steady_clock::now();
    cv::Mat image = cv::imread(key, cv::IMREAD_COLOR);
    stats_.decode_microseconds += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    if (image.empty()) {
        ++stats_.failures;
        return image;
    }
    ++stats_.decodes;
    const std::size_t bytes = image.total() * image.elemSize();
    if (bytes <= maximum_bytes_ - stats_.resident_bytes) {
        images_.emplace(key, CachedImage{image, {}});
        stats_.resident_bytes += bytes;
        stats_.peak_resident_bytes = std::max(stats_.peak_resident_bytes, stats_.resident_bytes);
    } else {
        ++stats_.uncached_decodes;
    }
    return image;
}

PageImageCacheStats PageImageCache::stats() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void PageImageCache::clear() {
    const std::lock_guard<std::mutex> lock(mutex_);
    images_.clear();
    stats_.resident_bytes = 0;
}

cv::Mat readPageImage(const document::PageArtifact& page) {
    if (const auto cache = page.image_cache.lock()) {
        return cache->read(page.output_path);
    }
    return cv::imread(page.output_path.string(), cv::IMREAD_COLOR);
}

} // namespace doc_parser::image
