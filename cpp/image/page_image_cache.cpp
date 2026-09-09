#include "image/page_image_cache.h"

#include <algorithm>
#include <chrono>
#include <opencv2/imgcodecs.hpp>

namespace doc_parser::image {

PageImageCache::PageImageCache(std::size_t maximum_bytes) : maximum_bytes_(maximum_bytes) {}

cv::Mat PageImageCache::read(const std::filesystem::path& path) {
    const std::string key = path.string();
    // Serialize loading too, so simultaneous consumers cannot decode and admit
    // the same image twice.
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = images_.find(key);
    if (found != images_.end()) {
        ++stats_.hits;
        return found->second;
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
        images_.emplace(key, image);
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
