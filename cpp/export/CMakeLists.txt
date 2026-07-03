add_library(doc_parser_export
    document_exporter.cpp
    json_document_exporter.cpp
)

target_include_directories(doc_parser_export
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser_export
    PUBLIC
        doc_parser_document
    PRIVATE
        nlohmann_json::nlohmann_json
)

doc_parser_set_common_warnings(doc_parser_export)
