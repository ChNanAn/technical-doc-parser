#pragma once

#include "common/result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace doc_parser::pipeline {

struct PipelineTraceEvent {
    std::string stage;
    std::string status;
    std::string message;
};

class PipelineTrace {
public:
    void record(std::string stage, std::string status, std::string message = {});
    const std::vector<PipelineTraceEvent>& events() const;

    common::Status write(const std::filesystem::path& output_path) const;

private:
    std::vector<PipelineTraceEvent> events_;
};

} // namespace doc_parser::pipeline
