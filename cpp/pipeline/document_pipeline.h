#pragma once

#include "app/cli_options.h"

namespace doc_parser::pipeline {

class DocumentPipeline {
public:
    bool run(const app::CliOptions& options) const;
};

} // namespace doc_parser::pipeline
