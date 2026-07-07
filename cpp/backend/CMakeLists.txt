add_library(doc_parser_backend_document
    document_backend_factory.cpp
)

target_include_directories(doc_parser_backend_document
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser_backend_document
    PUBLIC
        doc_parser_pipeline
)

if(DOC_PARSER_ENABLE_PDFIUM)
    add_subdirectory(pdf)

    target_link_libraries(doc_parser_backend_document
        PRIVATE
            doc_parser_backend_pdf_pdfium
    )

    target_compile_definitions(doc_parser_backend_document
        PRIVATE
            DOC_PARSER_ENABLE_PDFIUM=1
    )
endif()

doc_parser_set_common_warnings(doc_parser_backend_document)
