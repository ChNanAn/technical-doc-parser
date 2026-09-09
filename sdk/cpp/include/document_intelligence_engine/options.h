#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace doc_parser::pipeline {

struct RenderOptions {
    int dpi = 200;
};

struct BackendOptions {
    std::string document = "auto";
    std::string ocr = "auto";
    std::string layout = "auto";
    std::string table = "auto";
    std::filesystem::path registry_config;
};

struct DocumentParseOptions {
    std::filesystem::path input_path;
    std::filesystem::path output_directory = "output";
    RenderOptions render;
    bool debug = false;
    int timeout_seconds = 0;
    int maximum_pages = 0;
    std::string run_id;
    // Maximum retained RGBA/BGR page bytes; cleared after each page's table stage.
    // Rendered RGBA may fall back to PNG decoding if only BGR fits. 0 disables retention.
    std::size_t image_cache_bytes = 64 * 1024 * 1024;
};

} // namespace doc_parser::pipeline
