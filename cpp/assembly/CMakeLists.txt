add_library(doc_parser_assembly
    document_assembler.cpp
)

target_include_directories(doc_parser_assembly
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser_assembly
    PUBLIC
        doc_parser_document
)

doc_parser_set_common_warnings(doc_parser_assembly)
