#pragma once

#include <fpdf_text.h>
#include <fpdfview.h>

#include <memory>
#include <type_traits>

namespace doc_parser::pdf::detail {

struct PdfPageDeleter {
    void operator()(FPDF_PAGE page) const {
        if (page != nullptr) {
            FPDF_ClosePage(page);
        }
    }
};

struct PdfBitmapDeleter {
    void operator()(FPDF_BITMAP bitmap) const {
        if (bitmap != nullptr) {
            FPDFBitmap_Destroy(bitmap);
        }
    }
};

struct PdfTextPageDeleter {
    void operator()(FPDF_TEXTPAGE text_page) const {
        if (text_page != nullptr) {
            FPDFText_ClosePage(text_page);
        }
    }
};

using ScopedPdfPage = std::unique_ptr<std::remove_pointer_t<FPDF_PAGE>, PdfPageDeleter>;
using ScopedPdfBitmap = std::unique_ptr<std::remove_pointer_t<FPDF_BITMAP>, PdfBitmapDeleter>;
using ScopedPdfTextPage = std::unique_ptr<std::remove_pointer_t<FPDF_TEXTPAGE>, PdfTextPageDeleter>;

}  // namespace doc_parser::pdf::detail
