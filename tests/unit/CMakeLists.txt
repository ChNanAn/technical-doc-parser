include(GoogleTest)

add_executable(domain_unit_tests
    json_document_exporter_test.cpp
    text_normalizer_test.cpp
    text_extraction_stage_test.cpp
    ${PROJECT_SOURCE_DIR}/cpp/pipeline/text_extraction_stage.cpp
)

target_link_libraries(domain_unit_tests
    PRIVATE
        doc_parser_document
        doc_parser_export
        doc_parser_ocr
        GTest::gtest_main
        nlohmann_json::nlohmann_json
)

doc_parser_set_common_warnings(domain_unit_tests)

gtest_discover_tests(domain_unit_tests)
