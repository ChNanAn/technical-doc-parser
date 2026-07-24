#include "pipeline/pipeline_context.h"

#include <cstdlib>

namespace doc_parser::pipeline {

PipelineContext PipelineContext::fromOptions(const PipelineRunOptions& options) {
    PipelineContext context;
    context.input_path = options.input_path;
    context.render = options.render;
    context.debug = options.debug;
    context.backends = options.backends;
    if (!options.backends.registry_config.empty()) {
        context.backends.registry_config = options.backends.registry_config;
    } else if (const char* config = std::getenv("DOCUMENT_INTELLIGENCE_ENGINE_BACKEND_CONFIG"); config != nullptr) {
        context.backends.registry_config = config;
    }

    context.output.root = options.output_directory;
    context.output.pages_dir = context.output.root / "pages";
    context.output.debug_dir = context.output.root / "debug";
    context.output.manifest_json = context.output.root / "document.json";

    return context;
}

} // namespace doc_parser::pipeline
