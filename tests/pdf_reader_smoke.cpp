#include "document_source/pdf/pdf_document.h"

#include <iostream>

int main() {
    doc_parser::pdf::PdfDocument document;

    if (document.open("tests/fixtures/does-not-exist.pdf")) {
        std::cerr << "expected missing PDF to fail\n";
        return 1;
    }

    if (document.pageCount() != 0) {
        std::cerr << "expected missing PDF page count to be 0\n";
        return 1;
    }

    return 0;
}
