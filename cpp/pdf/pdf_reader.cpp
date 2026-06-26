#include "pdf/pdf_reader.h"

#include "pdf/pdfium_runtime.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace doc_parser::pdf {

PdfReader::~PdfReader() {
    std::lock_guard<std::mutex> lock(detail::pdfiumMutex());
    if (document_ != nullptr) {
        FPDF_CloseDocument(document_);
        document_ = nullptr;
    }
}

bool PdfReader::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(detail::pdfiumMutex());
    if (document_ != nullptr) {
        FPDF_CloseDocument(document_);
        document_ = nullptr;
    }

    document_ = FPDF_LoadDocument(path.c_str(), nullptr);
    return document_ != nullptr;
}

int PdfReader::pageCount() const {
    std::lock_guard<std::mutex> lock(detail::pdfiumMutex());
    if (document_ == nullptr) {
        return 0;
    }

    return FPDF_GetPageCount(document_);
}

bool PdfReader::renderPageToPng(int page_index, int dpi, const std::string& output_path) const {
    if (dpi <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(detail::pdfiumMutex());
    if (document_ == nullptr || page_index < 0 || page_index >= FPDF_GetPageCount(document_)) {
        return false;
    }

    FPDF_PAGE page = FPDF_LoadPage(document_, page_index);
    if (page == nullptr) {
        return false;
    }

    const double scale = static_cast<double>(dpi) / 72.0;
    const int width = static_cast<int>(std::lround(FPDF_GetPageWidthF(page) * scale));
    const int height = static_cast<int>(std::lround(FPDF_GetPageHeightF(page) * scale));
    if (width <= 0 || height <= 0) {
        FPDF_ClosePage(page);
        return false;
    }

    FPDF_BITMAP bitmap = FPDFBitmap_Create(width, height, 1);
    if (bitmap == nullptr) {
        FPDF_ClosePage(page);
        return false;
    }

    FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, 0, 0);

    const auto* bgra = static_cast<const std::uint8_t*>(FPDFBitmap_GetBuffer(bitmap));
    const int stride = FPDFBitmap_GetStride(bitmap);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);

    for (int y = 0; y < height; ++y) {
        const std::uint8_t* src = bgra + static_cast<std::size_t>(y) * stride;
        std::uint8_t* dst = rgba.data() + static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }

    const int ok = stbi_write_png(output_path.c_str(), width, height, 4, rgba.data(), width * 4);

    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    return ok != 0;
}

}  // namespace doc_parser::pdf
