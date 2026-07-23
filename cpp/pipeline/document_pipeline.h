#pragma once

#include "app/cli_options.h"
#include "pipeline/stage_observer.h"

namespace doc_parser::pipeline {

class DocumentPipeline {
public:
    bool run(const app::CliOptions& options) const;
    bool run(const app::CliOptions& options, IStageObserver& observer) const;
};

} // namespace doc_parser::pipeline
