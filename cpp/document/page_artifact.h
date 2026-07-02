#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace doc_parser::document {

struct PageBitmap {
    int page_index = 0;
    int page_number = 0;
    int width = 0;
    int height = 0;
    int channels = 4;
    std::vector<unsigned char> pixels;
};

struct PageArtifact {
    int page_index = 0;
    int page_number = 0;
    std::string relative_image;
    std::filesystem::path output_path;
};

} // namespace doc_parser::document
