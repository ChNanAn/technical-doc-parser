#include "backend/pdf/pdf_document.h"

namespace doc_parser::pdf {

bool PdfDocument::open(const std::string& path) { return reader_.open(path); }

bool PdfDocument::isOpen() const { return reader_.isOpen(); }

int PdfDocument::pageCount() const { return reader_.pageCount(); }

const PdfReader& PdfDocument::reader() const { return reader_; }

} // namespace doc_parser::pdf
