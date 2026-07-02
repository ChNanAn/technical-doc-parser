#include "pdf/pdf_reader.h"

#include "pdf/pdfium_runtime.h"

namespace doc_parser::pdf {

PdfReader::~PdfReader() { close(); }

void PdfReader::close() {
    std::lock_guard<std::mutex> lock(detail::pdfiumMutex());
    if (document_ != nullptr) {
        FPDF_CloseDocument(document_);
        document_ = nullptr;
    }
}

bool PdfReader::isOpen() const {
    std::lock_guard<std::mutex> lock(detail::pdfiumMutex());
    return document_ != nullptr;
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

} // namespace doc_parser::pdf
