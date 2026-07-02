#pragma once

#include "pdf/pdf_library.h"
#include "pdf/pdf_reader.h"

#include <string>

namespace doc_parser::pdf {

// Public PDF document facade. It binds PDFium runtime lifetime to the reader
// lifetime so callers cannot accidentally use PdfReader without initialized
// PDFium.
class PdfDocument {
public:
    PdfDocument() = default;
    ~PdfDocument() = default;

    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    PdfDocument(PdfDocument&&) = delete;
    PdfDocument& operator=(PdfDocument&&) = delete;

    bool open(const std::string& path);
    bool isOpen() const;
    int pageCount() const;

    const PdfReader& reader() const;

private:
    PdfLibrary library_;
    PdfReader reader_;
};

} // namespace doc_parser::pdf
