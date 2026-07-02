#pragma once

namespace doc_parser::pdf {

// Manages the process-wide PDFium lifetime. Create one instance near
// application startup and keep it alive longer than all PdfReader objects.
class PdfLibrary {
public:
    PdfLibrary();
    ~PdfLibrary();

    PdfLibrary(const PdfLibrary&) = delete;
    PdfLibrary& operator=(const PdfLibrary&) = delete;

    PdfLibrary(PdfLibrary&&) = delete;
    PdfLibrary& operator=(PdfLibrary&&) = delete;
};

} // namespace doc_parser::pdf
