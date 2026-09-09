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
    std::size_t resident_bytes = 0;
    std::size_t peak_resident_bytes = 0;
    std::uint64_t decode_microseconds = 0;
};

// One parse owns this cache. The pipeline clears completed pages after table
// recognition. Rendered files must be immutable between clears. Admission never
// evicts live pages; the limit covers retained pixels, not the current uncached
// image or inference tensors.
class PageImageCache {
public:
    explicit PageImageCache(std::size_t maximum_bytes);

    // Returns shared BGR pixels with cv::IMREAD_COLOR semantics. Treat the
    // returned matrix (including ROIs) as read-only; clone before modifying it.
    cv::Mat read(const std::filesystem::path& path);
    PageImageCacheStats stats() const;
    // Release completed pages while retaining cumulative measurements.
    void clear();

private:
    const std::size_t maximum_bytes_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, cv::Mat> images_;
    PageImageCacheStats stats_;
};

// Standalone backend calls and artifacts from a finished parse use the file.
cv::Mat readPageImage(const document::PageArtifact& page);

} // namespace doc_parser::image
