add_library(doc_parser_backend_pdf_pdfium
    pdf_document.cpp
    pdfium/pdf_library.cpp
    pdfium/pdf_page_renderer.cpp
    pdfium/pdf_reader.cpp
    pdfium/pdf_text_extractor.cpp
    pdfium/pdfium_document_backend.cpp
    pdfium/pdfium_runtime.cpp
    render_service.cpp
    text_service.cpp
)

target_include_directories(doc_parser_backend_pdf_pdfium
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_include_directories(doc_parser_backend_pdf_pdfium
    SYSTEM
    PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party/stb
)

target_link_libraries(doc_parser_backend_pdf_pdfium
    PUBLIC
        doc_parser_document
        pdfium
)

doc_parser_set_common_warnings(doc_parser_backend_pdf_pdfium)
