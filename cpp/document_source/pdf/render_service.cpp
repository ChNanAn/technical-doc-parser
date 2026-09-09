#include "document_source/pdf/render_service.h"

#include "document_source/pdf/pdfium/pdf_page_renderer.h"

#include <iostream>

namespace doc_parser::pdf {

bool RenderService::renderPage(const PdfDocument& source,
                               const RenderRequest& request,
                               int page_index,
                               document::PageArtifact& page) const {
    page = {};
    return source.isOpen() && PdfPageRenderer().renderPage(source.reader(), request, page_index, page);
}

bool RenderService::renderPages(const PdfDocument& source,
                                const RenderRequest& request,
                                std::vector<document::PageArtifact>& pages) const {
    pages.clear();

    if (!source.isOpen()) {
        return false;
    }

    PdfPageRenderer page_renderer;
    if (!page_renderer.renderPages(source.reader(), request, pages)) {
        std::cerr << "error: failed to render pages" << '\n';
        return false;
    }

    return true;
}

} // namespace doc_parser::pdf
