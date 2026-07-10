add_library(doc_parser_document_source_pdf
    pdf_document_backend.cpp
    pdf_document.cpp
    pdfium/pdf_library.cpp
    pdfium/pdf_page_renderer.cpp
    pdfium/pdf_reader.cpp
    pdfium/pdf_text_extractor.cpp
    pdfium/pdfium_runtime.cpp
    render_service.cpp
    text_service.cpp
)

target_include_directories(doc_parser_document_source_pdf
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_include_directories(doc_parser_document_source_pdf
    SYSTEM
    PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party/stb
)

target_link_libraries(doc_parser_document_source_pdf
    PUBLIC
        doc_parser_document
        pdfium
)

doc_parser_set_common_warnings(doc_parser_document_source_pdf)
