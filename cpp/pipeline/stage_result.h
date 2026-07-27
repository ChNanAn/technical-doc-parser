#pragma once

#include "common/diagnostic.h"
#include "common/status.h"

#include <vector>

namespace doc_parser::pipeline {

template <typename T> struct StageResult {
    common::Status status = common::Status::ok();
    T value;
    std::vector<common::Diagnostic> diagnostics;

    bool ok() const { return status.okStatus(); }
};

} // namespace doc_parser::pipeline
