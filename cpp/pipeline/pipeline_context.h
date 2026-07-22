#pragma once

#include "app/cli_options.h"

#include <filesystem>
#include <string>

namespace doc_parser::pipeline {

struct RenderOptions {
    int dpi = 200;
};

struct OutputPaths {
    std::filesystem::path root;
    std::filesystem::path pages_dir;
    std::filesystem::path debug_dir;
    std::filesystem::path manifest_json;
};

struct BackendOptions {
    std::string document = "auto";
    std::string ocr = "auto";
    std::string layout = "auto";
    std::string table = "auto";
    std::filesystem::path registry_config;
};

struct PipelineContext {
    std::filesystem::path input_pdf;
    RenderOptions render;
    OutputPaths output;
    BackendOptions backends;
    bool debug = false;

    static PipelineContext fromOptions(const app::CliOptions& options);
};

} // namespace doc_parser::pipeline
