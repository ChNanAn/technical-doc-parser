add_library(doc_parser_reading_order
    docling_like_reading_order_backend.cpp
)

doc_parser_set_public_includes(doc_parser_reading_order)

target_link_libraries(doc_parser_reading_order
    PUBLIC
        doc_parser_document
)

doc_parser_set_common_warnings(doc_parser_reading_order)
