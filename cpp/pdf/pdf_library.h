#pragma once

namespace doc_parser::pdf {

class PdfLibrary {
public:
    PdfLibrary();
    ~PdfLibrary();

    PdfLibrary(const PdfLibrary&) = delete;
    PdfLibrary& operator=(const PdfLibrary&) = delete;

    PdfLibrary(PdfLibrary&&) = delete;
    PdfLibrary& operator=(PdfLibrary&&) = delete;
};

}  // namespace doc_parser::pdf
