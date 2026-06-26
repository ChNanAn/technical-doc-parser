#include "pdf/pdf_library.h"

#include "pdf/pdfium_runtime.h"

#include <fpdfview.h>

namespace doc_parser::pdf {

PdfLibrary::PdfLibrary() {
    std::lock_guard<std::mutex> lock(detail::pdfiumMutex());
    FPDF_InitLibrary();
}

PdfLibrary::~PdfLibrary() {
    std::lock_guard<std::mutex> lock(detail::pdfiumMutex());
    FPDF_DestroyLibrary();
}

}  // namespace doc_parser::pdf
