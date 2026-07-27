#pragma once

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
};

} // namespace doc_parser::pipeline
