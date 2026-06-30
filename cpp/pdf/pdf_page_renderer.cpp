#include "pdf/pdf_page_renderer.h"

#include <cstddef>
#include <filesystem>
#include <iostream>

namespace doc_parser::pdf {

bool PdfPageRenderer::renderPages(
    const PdfReader& reader,
    const RenderRequest& request,
    std::vector<RenderedPage>& pages
) const {
    pages.clear();

    std::error_code ec;
    std::filesystem::create_directories(request.pages_dir, ec);
    if (ec) {
        std::cerr << "error: failed to create output directory: " << request.pages_dir << '\n';
        return false;
    }

    const int page_count = reader.pageCount();
    pages.reserve(static_cast<std::size_t>(page_count));

    for (int page_index = 0; page_index < page_count; ++page_index) {
        const std::string relative_image = "pages/page_" + std::to_string(page_index + 1) + ".png";
        const auto output_path = request.output_root / std::filesystem::path(relative_image);

        if (!reader.renderPageToPng(page_index, request.dpi, output_path.string())) {
            std::cerr << "error: failed to render page " << page_index + 1 << '\n';
            return false;
        }

        pages.push_back({
            page_index,
            page_index + 1,
            relative_image,
            output_path,
        });
    }

    return true;
}

}  // namespace doc_parser::pdf
