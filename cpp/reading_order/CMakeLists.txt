add_library(doc_parser_reading_order
    docling_like_reading_order_backend.cpp
    reading_order_service.cpp
)

target_include_directories(doc_parser_reading_order
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser_reading_order
    PUBLIC
        doc_parser_document
)

doc_parser_set_common_warnings(doc_parser_reading_order)
