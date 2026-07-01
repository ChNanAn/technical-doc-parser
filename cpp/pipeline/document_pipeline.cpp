#include "pipeline/document_pipeline.h"

#include "pipeline/pipeline_context.h"

#if DOC_PARSER_ENABLE_PDFIUM
#include "document/text_model.h"
#include "pdf/pdf_library.h"
#include "pdf/pdf_reader.h"
#include "pdf/render_service.h"
#include "pdf/text_service.h"
#include "pipeline/text_extraction_stage.h"
#endif

#include <cstddef>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>

namespace doc_parser::pipeline {
namespace {

#if DOC_PARSER_ENABLE_PDFIUM
const char* textSourceToString(document::TextSource source) {
    switch (source) {
        case document::TextSource::PdfTextLayer:
            return "pdf_text_layer";
        case document::TextSource::Ocr:
            return "ocr";
        case document::TextSource::Unknown:
            return "unknown";
    }
    return "unknown";
}

nlohmann::json bboxToJson(const document::BBox& bbox) {
    return {
        {"x0", bbox.x0},
        {"y0", bbox.y0},
        {"x1", bbox.x1},
        {"y1", bbox.y1},
    };
}

nlohmann::json pageTextToJson(const document::PageText& page_text) {
    nlohmann::json lines = nlohmann::json::array();
    for (const auto& line : page_text.lines) {
        nlohmann::json spans = nlohmann::json::array();
        for (const auto& span : line.spans) {
            spans.push_back({
                {"text", span.text},
                {"bbox", bboxToJson(span.bbox)},
                {"source", textSourceToString(span.source)},
                {"confidence", span.confidence},
            });
        }

        lines.push_back({
            {"text", line.text},
            {"bbox", bboxToJson(line.bbox)},
            {"source", textSourceToString(line.source)},
            {"confidence", line.confidence},
            {"spans", spans},
        });
    }

    return {
        {"has_text", page_text.has_text},
        {"preferred_source", textSourceToString(page_text.preferred_source)},
        {"lines", lines},
    };
}
#endif

}  // namespace

bool DocumentPipeline::run(const app::CliOptions& options) const {
    const PipelineContext context = PipelineContext::fromOptions(options);

#if DOC_PARSER_ENABLE_PDFIUM
    const std::string input_pdf_path = context.input_pdf.string();

    pdf::PdfLibrary library;  // PDFium process init
    pdf::PdfReader source;
    if (!source.open(input_pdf_path)) {
        std::cerr << "error: failed to open PDF: " << context.input_pdf << '\n';
        return false;
    }

    std::cout << "input_pdf: " << input_pdf_path << '\n'
              << "output_dir: " << context.output.root.string() << '\n'
              << "dpi: " << context.render.dpi << '\n'
              << "debug: " << (context.debug ? "true" : "false") << '\n'
              << "pages: " << source.pageCount() << '\n';

    TextExtractionStage text_extraction;
    std::vector<document::PageText> page_texts;
    if (!text_extraction.extract(source, context.render.dpi, page_texts)) {
        return false;
    }

    pdf::RenderService render;
    std::vector<pdf::RenderedPage> rendered_pages;
    if (!render.renderPages(
            source,
            {
                context.render.dpi,
                context.output.root,
                context.output.pages_dir,
            },
            rendered_pages)) {
        return false;
    }

    nlohmann::json manifest;
    manifest["source"] = {
        {"path", input_pdf_path},
        {"type", "pdf"},
    };
    manifest["render"] = {
        {"dpi", context.render.dpi},
    };
    manifest["pages"] = nlohmann::json::array();

    for (std::size_t index = 0; index < rendered_pages.size(); ++index) {
        const auto& page = rendered_pages[index];
        nlohmann::json page_json = {
            {"page_index", page.page_index},
            {"page_number", page.page_number},
            {"image", page.relative_image},
        };
        if (context.debug) {
            page_json["debug"]["text"] = pageTextToJson(page_texts[index]);
        }

        manifest["pages"].push_back(page_json);
        std::cout << "wrote: " << page.output_path.string() << '\n';
    }

    std::ofstream manifest_file(context.output.manifest_json);
    if (!manifest_file) {
        std::cerr << "error: failed to write manifest: " << context.output.manifest_json << '\n';
        return false;
    }
    manifest_file << manifest.dump(2) << '\n';
    std::cout << "wrote: " << context.output.manifest_json.string() << '\n';
    return true;
#else
    std::cout << "input_pdf: " << context.input_pdf.string() << '\n'
              << "output_dir: " << context.output.root.string() << '\n'
              << "dpi: " << context.render.dpi << '\n'
              << "debug: " << (context.debug ? "true" : "false") << '\n';
    return true;
#endif
}

}  // namespace doc_parser::pipeline
