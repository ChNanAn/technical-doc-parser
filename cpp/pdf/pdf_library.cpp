#include "pdf/pdf_library.h"

#include <fpdfview.h>

namespace doc_parser::pdf {

PdfLibrary::PdfLibrary() {
    FPDF_InitLibrary();
}

PdfLibrary::~PdfLibrary() {
    FPDF_DestroyLibrary();
}

}  // namespace doc_parser::pdf
