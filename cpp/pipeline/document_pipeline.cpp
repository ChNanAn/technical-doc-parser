#include "pipeline/document_pipeline.h"

#include "assembly/document_assembler.h"
#include "document/parsed_document.h"
#include "export/document_exporter.h"
#include "pipeline/layout_analysis_stage.h"
#include "pipeline/pipeline_context.h"
#include "pipeline/pipeline_service_factory.h"
#include "pipeline/reading_order_stage.h"
#include "pipeline/table_recognition_stage.h"
#include "pipeline/text_extraction_stage.h"

#if DOC_PARSER_ENABLE_OPENCV
#include "image/image_preprocessor.h"
#endif

#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace doc_parser::pipeline {
namespace {

#if DOC_PARSER_ENABLE_OPENCV
std::string relativeToOutputRoot(const std::filesystem::path& path, const PipelineContext& context) {
    const std::filesystem::path relative_path = path.lexically_relative(context.output.root);
    if (relative_path.empty()) {
        return path.filename().generic_string();
    }
    return relative_path.generic_string();
}
#endif

bool preprocessDebugImages(const PipelineContext& context, std::vector<document::PageArtifact>& pages) {
    if (!context.debug) {
        return true;
    }

#if DOC_PARSER_ENABLE_OPENCV
    const image::ImagePreprocessor preprocessor;
    for (auto& page : pages) {
        const std::filesystem::path output_path =
            context.output.debug_dir / ("page_" + std::to_string(page.page_number) + "_preprocessed.png");
        if (!preprocessor.preprocessFile(page.output_path, output_path)) {
            spdlog::error("failed to preprocess image for page {}", page.page_number);
            return false;
        }

        page.debug_images.push_back({
            "preprocessed",
            relativeToOutputRoot(output_path, context),
            output_path,
        });
        spdlog::info("wrote: {}", output_path.string());
    }
#else
    (void)pages;
#endif

    return true;
}

} // namespace

bool DocumentPipeline::run(const app::CliOptions& options) const {
    const PipelineContext context = PipelineContext::fromOptions(options);

    PipelineServiceCreationResult service_creation = createPipelineServices(context.backends);
    if (!service_creation.ok) {
        return false;
    }
    spdlog::info("configured services: {}", service_creation.trace_message);

    auto& document = service_creation.services.document;

    if (!document.source->open(context.input_pdf)) {
        spdlog::error("open_document: failed to open input document: {}", context.input_pdf.string());
        return false;
    }

    spdlog::info("input: {}", document.source->sourcePath());
    spdlog::info("output_dir: {}", context.output.root.string());
    spdlog::info("dpi: {}", context.render.dpi);
    spdlog::info("debug: {}", context.debug);
    spdlog::info("pages: {}", document.source->pageCount());

    if (document.renderer == nullptr) {
        spdlog::error("render_pages: document source cannot render pages");
        return false;
    }

    std::vector<document::PageArtifact> rendered_pages;
    if (!document.renderer->renderPages({context.render.dpi, context.output.root, context.output.pages_dir},
                                        rendered_pages)) {
        spdlog::error("render_pages: failed to render page artifacts");
        return false;
    }
    spdlog::info("rendered pages: {}", rendered_pages.size());

    for (const auto& page : rendered_pages) {
        spdlog::info("wrote: {}", page.output_path.string());
    }

    if (!preprocessDebugImages(context, rendered_pages)) {
        spdlog::error("preprocess_debug_images: failed to write debug preprocessing images");
        return false;
    }
    spdlog::debug("preprocessed debug images");

    const TextExtractionStage text_extraction(document.native_text_extractor, *service_creation.services.ocr);
    std::vector<document::PageText> page_texts;
    common::Status stage_status = text_extraction.extract(context, rendered_pages, page_texts);
    if (!stage_status.okStatus()) {
        spdlog::error("text_extraction: {}", stage_status.message());
        return false;
    }
    spdlog::info("extracted text pages: {}", page_texts.size());

    const LayoutAnalysisStage layout_analysis(*service_creation.services.layout);
    std::vector<document::PageLayout> page_layouts;
    stage_status = layout_analysis.analyze(context, rendered_pages, page_texts, page_layouts);
    if (!stage_status.okStatus()) {
        spdlog::error("layout_analysis: {}", stage_status.message());
        return false;
    }
    spdlog::info("analyzed layout pages: {}", page_layouts.size());

    const TableRecognitionStage table_recognition(*service_creation.services.table);
    std::vector<document::PageTables> page_tables;
    stage_status = table_recognition.recognize(context, rendered_pages, page_texts, page_layouts, page_tables);
    if (!stage_status.okStatus()) {
        spdlog::error("table_recognition: {}", stage_status.message());
        return false;
    }
    spdlog::info("recognized table pages: {}", page_tables.size());

    const ReadingOrderStage reading_order(*service_creation.services.reading_order);
    std::vector<document::PageReadingOrder> page_reading_orders;
    stage_status = reading_order.order(context, rendered_pages, page_layouts, page_reading_orders);
    if (!stage_status.okStatus()) {
        spdlog::error("reading_order: {}", stage_status.message());
        return false;
    }
    spdlog::info("computed reading order pages: {}", page_reading_orders.size());

    document::ParsedDocument parsed_document;
    document::PipelineArtifacts artifacts;
    const assembly::DocumentAssembler document_assembler;
    if (!document_assembler.assemble(
            {
                document.source->sourcePath(),
                document.source->sourceType(),
                context.render.dpi,
                rendered_pages,
                page_texts,
                page_layouts,
                page_reading_orders,
                page_tables,
            },
            parsed_document,
            artifacts)) {
        spdlog::error("document_assembly: failed to assemble document");
        return false;
    }
    std::size_t detected_furniture = 0;
    for (const document::PageLayout& layout : page_layouts) {
        for (const document::LayoutBlock& block : layout.blocks) {
            if (block.type == document::LayoutBlockType::Header || block.type == document::LayoutBlockType::Footer) {
                ++detected_furniture;
            }
        }
    }
    std::size_t emitted_furniture = 0;
    for (const document::DocumentBlock& block : parsed_document.blocks) {
        if (block.type == document::DocumentBlockType::Header || block.type == document::DocumentBlockType::Footer) {
            ++emitted_furniture;
        }
    }
    spdlog::debug("document_assembly: repeated_header_footer_removed={}",
                  detected_furniture >= emitted_furniture ? detected_furniture - emitted_furniture : 0U);
    spdlog::info("assembled document blocks: {}", parsed_document.blocks.size());

    const auto document_exporter = exporter::createDefaultDocumentExporter();
    if (document_exporter == nullptr) {
        spdlog::error("export: no document exporter is enabled");
        return false;
    }

    if (!document_exporter->write({
            context.debug,
            context.output.manifest_json,
            &parsed_document,
            &artifacts,
        })) {
        spdlog::error("export: failed to write document manifest");
        return false;
    }

    spdlog::info("wrote: {}", context.output.manifest_json.string());
    std::filesystem::path markdown_path = context.output.manifest_json;
    std::filesystem::path html_path = context.output.manifest_json;
    spdlog::info("wrote: {}", markdown_path.replace_extension(".md").string());
    spdlog::info("wrote: {}", html_path.replace_extension(".html").string());
    return true;
}

} // namespace doc_parser::pipeline
