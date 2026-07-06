#pragma once

#include "pipeline/stage_interfaces.h"

#include <memory>
#include <string>

namespace doc_parser::pipeline {

std::unique_ptr<IDocumentBackend> createDocumentBackend(const std::string& backend_name);
std::unique_ptr<IDocumentBackend> createDefaultDocumentBackend();

} // namespace doc_parser::pipeline
