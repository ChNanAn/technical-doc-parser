#pragma once

#include <cstddef>
#include <string>

namespace doc_parser::app {

struct CliOptions {
    std::string input_pdf;
    std::string output_dir = "output";
    std::string run_id;
    int dpi = 200;
    bool debug = false;
    std::string document_backend = "auto";
    std::string ocr_backend = "auto";
    std::string layout_backend = "auto";
    std::string table_backend = "auto";
    std::string backend_config;
    int timeout_seconds = 0;
    int maximum_pages = 0;
    std::size_t image_cache_bytes = 64 * 1024 * 1024;
};

} // namespace doc_parser::app
