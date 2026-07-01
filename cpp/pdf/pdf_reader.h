#pragma once

#include <fpdfview.h>

#include <string>

namespace doc_parser::pdf {

class PdfTextExtractor;

// Owns one FPDF_DOCUMENT. PdfReader instances are not thread-safe and must not
// be shared across threads. Different readers may be used by different threads;
// direct PDFium calls are serialized internally by the PDF module.
class PdfReader {
public:
    PdfReader() = default;
    ~PdfReader();

    PdfReader(const PdfReader&) = delete;
    PdfReader& operator=(const PdfReader&) = delete;

    PdfReader(PdfReader&&) = delete;
    PdfReader& operator=(PdfReader&&) = delete;

    bool open(const std::string& path);
    void close();
    bool isOpen() const;
    int pageCount() const;
    bool renderPageToPng(int page_index, int dpi, const std::string& output_path) const;

private:
    friend class PdfTextExtractor;

    FPDF_DOCUMENT document_ = nullptr;
};

}  // namespace doc_parser::pdf
