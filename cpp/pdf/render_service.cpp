#include "pdf/render_service.h"

#include "pdf/pdf_page_renderer.h"

#include <iostream>

namespace doc_parser::pdf {

bool RenderService::renderPages(const PdfReader& source,
                                const RenderRequest& request,
                                std::vector<RenderedPage>& pages) const {
    pages.clear();

    if (!source.isOpen()) {
        return false;
    }

    PdfPageRenderer page_renderer;
    if (!page_renderer.renderPages(source, request, pages)) {
        std::cerr << "error: failed to render pages" << '\n';
        return false;
    }

    return true;
}

}  // namespace doc_parser::pdf
