#pragma once

#include <document_intelligence_engine/options.h>

namespace doc_parser::pipeline {

struct PipelineRunOptions : DocumentParseOptions {
    BackendOptions backends;
};

} // namespace doc_parser::pipeline
