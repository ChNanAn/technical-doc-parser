#include "pipeline/pipeline_context.h"

namespace doc_parser::pipeline {

PipelineContext PipelineContext::fromOptions(const app::CliOptions& options) {
    PipelineContext context;
    context.input_pdf = options.input_pdf;
    context.render.dpi = options.dpi;
    context.debug = options.debug;
    context.backends.document = options.document_backend;
    context.backends.ocr = options.ocr_backend;
    context.backends.layout = options.layout_backend;
    context.backends.table = options.table_backend;

    context.output.root = options.output_dir;
    context.output.pages_dir = context.output.root / "pages";
    context.output.debug_dir = context.output.root / "debug";
    context.output.manifest_json = context.output.root / "document.json";

    return context;
}

} // namespace doc_parser::pipeline
