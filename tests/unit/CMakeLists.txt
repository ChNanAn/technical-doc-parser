include(GoogleTest)

add_executable(domain_unit_tests
    atomic_output_file_test.cpp
    backend_registry_test.cpp
    document_assembler_test.cpp
    document_engine_test.cpp
    page_pipeline_test.cpp
    file_fingerprint_test.cpp
    json_document_exporter_test.cpp
    layout_backend_test.cpp
    layout_postprocessing_test.cpp
    orientation_policy_test.cpp
    reading_order_backend_test.cpp
    structured_text_document_exporter_test.cpp
    table_backend_test.cpp
    table_recognition_stage_test.cpp
    text_normalizer_test.cpp
    text_extraction_stage_test.cpp
    utf8_test.cpp
    warning_aggregator_test.cpp
    warning_codes_test.cpp
)

target_link_libraries(domain_unit_tests
    PRIVATE
        doc_parser_assembly
        doc_parser_common
        doc_parser_document
        doc_parser_export
        doc_parser_layout
        doc_parser_ocr
        doc_parser_pipeline
        doc_parser_reading_order
        doc_parser_table
        document_intelligence_engine_sdk
        GTest::gtest_main
        nlohmann_json::nlohmann_json
)

target_compile_definitions(domain_unit_tests
    PRIVATE
        DOC_PARSER_TEST_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)

doc_parser_set_common_warnings(domain_unit_tests)

if(DOC_PARSER_ENABLE_OPENCV OR DOC_PARSER_ENABLE_ONNXRUNTIME)
    target_sources(domain_unit_tests PRIVATE page_image_cache_test.cpp)
    target_link_libraries(domain_unit_tests PRIVATE doc_parser_image)
endif()

gtest_discover_tests(domain_unit_tests)
