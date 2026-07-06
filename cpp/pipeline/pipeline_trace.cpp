#include "pipeline/pipeline_trace.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <utility>

namespace doc_parser::pipeline {

void PipelineTrace::record(std::string stage, std::string status, std::string message) {
    events_.push_back({
        std::move(stage),
        std::move(status),
        std::move(message),
    });
}

const std::vector<PipelineTraceEvent>& PipelineTrace::events() const { return events_; }

common::Status PipelineTrace::write(const std::filesystem::path& output_path) const {
    if (!output_path.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(output_path.parent_path(), ec);
        if (ec) {
            return common::Status::error("trace.create_directory_failed", "failed to create trace output directory");
        }
    }

    nlohmann::json events = nlohmann::json::array();
    for (const auto& event : events_) {
        events.push_back({
            {"stage", event.stage},
            {"status", event.status},
            {"message", event.message},
        });
    }

    std::ofstream output(output_path);
    if (!output) {
        return common::Status::error("trace.write_failed", "failed to open trace output file");
    }

    output << nlohmann::json{{"events", events}}.dump(2) << '\n';
    return common::Status::ok();
}

} // namespace doc_parser::pipeline
