add_library(doc_parser_document_source
    document_source_factory.cpp
)

target_include_directories(doc_parser_document_source
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser_document_source
    PUBLIC
        doc_parser_document
)

if(DOC_PARSER_ENABLE_PDFIUM)
    add_subdirectory(pdf)

    target_link_libraries(doc_parser_document_source
        PRIVATE
            doc_parser_document_source_pdf
    )

    target_compile_definitions(doc_parser_document_source
        PRIVATE
            DOC_PARSER_ENABLE_PDFIUM=1
    )
endif()

doc_parser_set_common_warnings(doc_parser_document_source)
