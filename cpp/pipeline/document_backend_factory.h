#pragma once

#include "pipeline/stage_interfaces.h"

#include <memory>

namespace doc_parser::pipeline {

std::unique_ptr<IDocumentBackend> createDefaultDocumentBackend();

} // namespace doc_parser::pipeline
