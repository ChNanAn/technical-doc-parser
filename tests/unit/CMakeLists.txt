include(GoogleTest)

add_executable(domain_unit_tests
    document_assembler_test.cpp
    json_document_exporter_test.cpp
    layout_service_test.cpp
    table_service_test.cpp
    text_normalizer_test.cpp
    text_extraction_stage_test.cpp
)

target_link_libraries(domain_unit_tests
    PRIVATE
        doc_parser_assembly
        doc_parser_document
        doc_parser_export
        doc_parser_layout
        doc_parser_ocr
        doc_parser_pipeline
        doc_parser_table
        GTest::gtest_main
        nlohmann_json::nlohmann_json
)

doc_parser_set_common_warnings(domain_unit_tests)

gtest_discover_tests(domain_unit_tests)
