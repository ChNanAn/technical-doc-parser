#pragma once

#include <fpdfview.h>

#include <string>

namespace doc_parser::pdf {

class PdfReader {
public:
    PdfReader() = default;
    ~PdfReader();

    PdfReader(const PdfReader&) = delete;
    PdfReader& operator=(const PdfReader&) = delete;

    PdfReader(PdfReader&&) = delete;
    PdfReader& operator=(PdfReader&&) = delete;

    bool open(const std::string& path);
    int pageCount() const;

private:
    FPDF_DOCUMENT document_ = nullptr;
};

}  // namespace doc_parser::pdf
