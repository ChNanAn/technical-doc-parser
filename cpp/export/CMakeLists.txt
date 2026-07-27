add_library(doc_parser_export
    document_exporter.cpp
    json_document_exporter.cpp
    structured_text_document_exporter.cpp
)

doc_parser_set_public_includes(doc_parser_export)

target_link_libraries(doc_parser_export
    PUBLIC
        doc_parser_common
        doc_parser_document
    PRIVATE
        $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>
)

doc_parser_set_common_warnings(doc_parser_export)
