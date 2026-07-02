add_library(doc_parser_document
    text_normalizer.cpp
)

target_include_directories(doc_parser_document
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

doc_parser_set_common_warnings(doc_parser_document)
