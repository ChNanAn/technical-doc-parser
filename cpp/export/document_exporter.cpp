#include "export/document_exporter.h"

#include "export/json_document_exporter.h"
#include "export/structured_text_document_exporter.h"

#include <utility>

namespace {

class MultiFormatDocumentExporter final : public doc_parser::exporter::IDocumentExporter {
public:
    doc_parser::common::Status write(const doc_parser::exporter::DocumentExportRequest& request) const override {
        if (doc_parser::common::Status status = json_.write(request); !status.okStatus()) {
            return status;
        }
        doc_parser::exporter::DocumentExportRequest markdown_request = request;
        markdown_request.output_path.replace_extension(".md");
        if (doc_parser::common::Status status = markdown_.write(markdown_request); !status.okStatus()) {
            return status;
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
