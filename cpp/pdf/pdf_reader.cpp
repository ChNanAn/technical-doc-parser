#include "pdf/pdf_reader.h"

namespace doc_parser::pdf {

PdfReader::~PdfReader() {
    if (document_ != nullptr) {
        FPDF_CloseDocument(document_);
        document_ = nullptr;
    }
}

bool PdfReader::open(const std::string& path) {
    if (document_ != nullptr) {
        FPDF_CloseDocument(document_);
        document_ = nullptr;
    }

    document_ = FPDF_LoadDocument(path.c_str(), nullptr);
    return document_ != nullptr;
}

int PdfReader::pageCount() const {
    if (document_ == nullptr) {
        return 0;
    }

    return FPDF_GetPageCount(document_);
}

}  // namespace doc_parser::pdf
