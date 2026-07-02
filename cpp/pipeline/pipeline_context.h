#pragma once

#include "app/cli_options.h"

#include <filesystem>

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

struct PipelineContext {
    std::filesystem::path input_pdf;
    RenderOptions render;
    OutputPaths output;
    bool debug = false;

    static PipelineContext fromOptions(const app::CliOptions& options);
};

} // namespace doc_parser::pipeline
