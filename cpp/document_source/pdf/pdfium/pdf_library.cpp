#include "document_source/pdf/pdfium/pdf_library.h"

#include "document_source/pdf/pdfium/pdfium_runtime.h"

#include <fpdfview.h>

namespace doc_parser::pdf {
namespace {

int& pdfLibraryRefCount() {
    static int ref_count = 0;
    return ref_count;
}

} // namespace

PdfLibrary::PdfLibrary() {
    std::lock_guard<std::mutex> lock(detail::pdfiumMutex());
    int& ref_count = pdfLibraryRefCount();
    if (ref_count == 0) {
        FPDF_InitLibrary();
    }
    ++ref_count;
}

PdfLibrary::~PdfLibrary() {
    std::lock_guard<std::mutex> lock(detail::pdfiumMutex());
    int& ref_count = pdfLibraryRefCount();
    if (ref_count <= 0) {
        return;
    }

    --ref_count;
    if (ref_count == 0) {
        FPDF_DestroyLibrary();
    }
}

} // namespace doc_parser::pdf
