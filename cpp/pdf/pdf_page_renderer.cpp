#include "pdf/pdf_page_renderer.h"

#include "pdf/pdf_reader.h"
#include "pdf/pdfium_runtime.h"
#include "pdf/pdfium_scoped_handles.h"
#include "pdf/render_service.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stb_image_write.h>

namespace doc_parser::pdf {
namespace {

bool writePng(const document::PageBitmap& bitmap, const std::filesystem::path& output_path) {
    if (bitmap.width <= 0 || bitmap.height <= 0 || bitmap.channels != 4 || bitmap.pixels.empty()) {
        return false;
    }

    const int ok = stbi_write_png(output_path.string().c_str(),
                                  bitmap.width,
                                  bitmap.height,
                                  bitmap.channels,
                                  bitmap.pixels.data(),
                                  bitmap.width * bitmap.channels);
    return ok != 0;
}

} // namespace

bool PdfPageRenderer::renderPages(const PdfReader& reader,
                                  const RenderRequest& request,
                                  std::vector<document::PageArtifact>& pages) const {
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

        document::PageBitmap bitmap;
        if (!renderPageBitmap(reader, page_index, request.dpi, bitmap) || !writePng(bitmap, output_path)) {
            std::cerr << "error: failed to render page " << page_index + 1 << '\n';
            return false;
        }

        pages.push_back({
            page_index,
            page_index + 1,
            relative_image,
            output_path,
            bitmap.width,
            bitmap.height,
            {},
        });
    }

    return true;
}

bool PdfPageRenderer::renderPageBitmap(const PdfReader& reader,
                                       int page_index,
                                       int dpi,
                                       document::PageBitmap& bitmap) const {
    bitmap = {};

    if (dpi <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(detail::pdfiumMutex());
    if (reader.document_ == nullptr || page_index < 0 || page_index >= FPDF_GetPageCount(reader.document_)) {
        return false;
    }

    detail::ScopedPdfPage page(FPDF_LoadPage(reader.document_, page_index));
    if (page == nullptr) {
        return false;
    }

    const double scale = static_cast<double>(dpi) / 72.0;
    const int width = static_cast<int>(std::lround(FPDF_GetPageWidthF(page.get()) * scale));
    const int height = static_cast<int>(std::lround(FPDF_GetPageHeightF(page.get()) * scale));
    if (width <= 0 || height <= 0) {
        return false;
    }

    detail::ScopedPdfBitmap pdf_bitmap(FPDFBitmap_Create(width, height, 1));
    if (pdf_bitmap == nullptr) {
        return false;
    }

    FPDFBitmap_FillRect(pdf_bitmap.get(), 0, 0, width, height, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(pdf_bitmap.get(), page.get(), 0, 0, width, height, 0, 0);

    bitmap.page_index = page_index;
    bitmap.page_number = page_index + 1;
    bitmap.width = width;
    bitmap.height = height;
    bitmap.channels = 4;
    bitmap.pixels.resize(static_cast<std::size_t>(width) * height * 4);

    const auto* bgra = static_cast<const std::uint8_t*>(FPDFBitmap_GetBuffer(pdf_bitmap.get()));
    const int stride = FPDFBitmap_GetStride(pdf_bitmap.get());
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* src = bgra + static_cast<std::size_t>(y) * stride;
        unsigned char* dst = bitmap.pixels.data() + static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }

    return true;
}

} // namespace doc_parser::pdf
