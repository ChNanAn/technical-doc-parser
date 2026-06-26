#include "pdf/pdf_library.h"
#include "pdf/pdf_reader.h"

#include <iostream>

int main() {
    doc_parser::pdf::PdfLibrary library;
    doc_parser::pdf::PdfReader reader;

    if (reader.open("tests/fixtures/does-not-exist.pdf")) {
        std::cerr << "expected missing PDF to fail\n";
        return 1;
    }

    if (reader.pageCount() != 0) {
        std::cerr << "expected missing PDF page count to be 0\n";
        return 1;
    }

    return 0;
}
