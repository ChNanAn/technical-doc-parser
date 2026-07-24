#pragma once

#include "pipeline/pipeline_options.h"

#include <filesystem>
#include <string>

namespace doc_parser::pipeline {

struct OutputPaths {
    std::filesystem::path root;
    std::filesystem::path pages_dir;
    std::filesystem::path debug_dir;
    std::filesystem::path manifest_json;
};

struct PipelineContext {
    std::filesystem::path input_path;
    RenderOptions render;
    OutputPaths output;
    BackendOptions backends;
    bool debug = false;

    static PipelineContext fromOptions(const PipelineRunOptions& options);
};

} // namespace doc_parser::pipeline
