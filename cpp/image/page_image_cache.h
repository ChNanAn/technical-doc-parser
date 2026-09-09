#pragma once

#include "document/page_artifact.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <opencv2/core.hpp>
#include <string>
#include <unordered_map>

namespace doc_parser::image {

struct PageImageCacheStats {
    std::size_t hits = 0;
    std::size_t decodes = 0;
    std::size_t failures = 0;
    std::size_t uncached_decodes = 0;
    std::size_t rendered_admissions = 0;
    std::size_t rendered_rejections = 0;
    std::size_t conversions = 0;
    std::size_t resident_bytes = 0;
    std::size_t peak_resident_bytes = 0;
    std::uint64_t decode_microseconds = 0;
    std::uint64_t conversion_microseconds = 0;
};

// One parse owns this cache. The pipeline clears completed pages after table
// recognition. Rendered files must be immutable between clears. Admission never
// evicts live pages; the limit covers retained RGBA/BGR pixels, not the current
// uncached image, temporary conversion output, or inference tensors.
class PageImageCache {
public:
    explicit PageImageCache(std::size_t maximum_bytes);

    // Returns shared BGR pixels with cv::IMREAD_COLOR semantics. Treat the
    // returned matrix (including ROIs) as read-only; clone before modifying it.
    cv::Mat read(const std::filesystem::path& path);
    // Move in validated RGBA pixels without decoding the PNG or converting yet.
    // Counts vector capacity against the budget. Unsupported, oversized or duplicate
    // entries are rejected without consuming the bitmap. Missing entries still use the file.
    bool admitRendered(const document::PageArtifact& page, document::PageBitmap&& bitmap);
    PageImageCacheStats stats() const;
    // Release completed pages while retaining cumulative measurements.
    void clear();

private:
    const std::size_t maximum_bytes_;
    mutable std::mutex mutex_;
    struct CachedImage {
        cv::Mat bgr;
        document::PageBitmap rendered;
    };
    std::unordered_map<std::string, CachedImage> images_;
    PageImageCacheStats stats_;
};

// Standalone backend calls and artifacts from a finished parse use the file.
cv::Mat readPageImage(const document::PageArtifact& page);

} // namespace doc_parser::image
