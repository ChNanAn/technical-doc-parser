#include "export/document_exporter.h"

#include "export/json_document_exporter.h"
#include "export/structured_text_document_exporter.h"

#include <utility>

namespace {

class MultiFormatDocumentExporter final : public doc_parser::exporter::IDocumentExporter {
public:
    bool write(const doc_parser::exporter::DocumentExportRequest& request) const override {
        if (!json_.write(request)) {
            return false;
        }
        doc_parser::exporter::DocumentExportRequest markdown_request = request;
        markdown_request.output_path.replace_extension(".md");
        if (!markdown_.write(markdown_request)) {
            return false;
        }
        doc_parser::exporter::DocumentExportRequest html_request = request;
        html_request.output_path.replace_extension(".html");
        return html_.write(html_request);
    }

private:
    doc_parser::exporter::JsonDocumentExporter json_;
    doc_parser::exporter::MarkdownDocumentExporter markdown_;
    doc_parser::exporter::HtmlDocumentExporter html_;
};

} // namespace

namespace doc_parser::exporter {

std::unique_ptr<IDocumentExporter> createDefaultDocumentExporter() {
    return std::make_unique<MultiFormatDocumentExporter>();
}

} // namespace doc_parser::exporter
