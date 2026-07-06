add_library(doc_parser_table
    table_service.cpp
    text_table_structure_backend.cpp
)

target_include_directories(doc_parser_table
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser_table
    PUBLIC
        doc_parser_document
)

doc_parser_set_common_warnings(doc_parser_table)
