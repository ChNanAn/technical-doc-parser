add_library(doc_parser_assembly
    document_assembler.cpp
)

doc_parser_set_public_includes(doc_parser_assembly)

target_link_libraries(doc_parser_assembly
    PUBLIC
        doc_parser_document
)

target_compile_definitions(doc_parser_assembly
    PRIVATE
        DOC_PARSER_ENGINE_VERSION="${PROJECT_VERSION}"
)

doc_parser_set_common_warnings(doc_parser_assembly)
