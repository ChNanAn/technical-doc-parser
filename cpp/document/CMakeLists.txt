add_library(doc_parser_document
    page_rotation.cpp
    text_normalizer.cpp
    warning_aggregator.cpp
)

doc_parser_set_public_includes(doc_parser_document)

doc_parser_set_common_warnings(doc_parser_document)
