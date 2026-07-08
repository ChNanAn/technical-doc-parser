add_library(doc_parser_layout
    text_layout_model_backend.cpp
)

target_include_directories(doc_parser_layout
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser_layout
    PUBLIC
        doc_parser_document
)

doc_parser_set_common_warnings(doc_parser_layout)
