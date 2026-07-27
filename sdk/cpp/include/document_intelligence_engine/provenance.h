#pragma once

#include "document_intelligence_engine/options.h"

#include <filesystem>
#include <string>
#include <vector>

namespace doc_parser::pipeline {

struct BackendResolution {
    BackendOptions requested;
    BackendOptions resolved;
    std::string config_source;
};

struct ModelProvenance {
    std::string stage;
    std::string backend;
    std::string role;
    std::filesystem::path path;
    std::string profile;
};

struct FallbackProvenance {
    std::string stage;
    int page_number = 0;
    std::string failed_backend;
    std::string fallback_backend;
    std::string reason;
};

struct RunProvenance {
    std::string run_id;
    std::string engine_name = "technical-doc-parser";
    std::string engine_version = "unknown";
    std::string git_revision;
    BackendResolution backends;
    std::vector<ModelProvenance> models;
    std::vector<FallbackProvenance> fallbacks;
};

} // namespace doc_parser::pipeline
