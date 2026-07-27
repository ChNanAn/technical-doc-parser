set(DOCUMENT_INTELLIGENCE_WORKER_ENGINE_SOURCES
    document_engine_cache.cpp
    worker_document_processor.cpp
)

add_executable(document_intelligence_worker
    ${DOCUMENT_INTELLIGENCE_WORKER_ENGINE_SOURCES}
    main.cpp
    redis_client.cpp
    worker_stage_observer.cpp
)

target_include_directories(document_intelligence_worker
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(document_intelligence_worker
    PRIVATE
        doc_parser_pipeline
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

doc_parser_set_common_warnings(document_intelligence_worker)

if(BUILD_TESTING)
    add_executable(platform_worker_unit_tests
        ${DOCUMENT_INTELLIGENCE_WORKER_ENGINE_SOURCES}
        document_engine_cache_test.cpp
        worker_stage_observer.cpp
        worker_stage_observer_test.cpp
    )

    target_include_directories(platform_worker_unit_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(platform_worker_unit_tests
        PRIVATE
            doc_parser_pipeline
            GTest::gtest_main
            nlohmann_json::nlohmann_json
            spdlog::spdlog
    )

    target_compile_definitions(platform_worker_unit_tests
        PRIVATE
            DOC_PARSER_TEST_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
    )
    if(DOC_PARSER_ENABLE_PDFIUM)
        target_compile_definitions(platform_worker_unit_tests PRIVATE DOC_PARSER_WORKER_TEST_PDFIUM=1)
    endif()

    doc_parser_set_common_warnings(platform_worker_unit_tests)
    add_test(NAME platform_worker_unit_tests COMMAND platform_worker_unit_tests)
    set_tests_properties(platform_worker_unit_tests PROPERTIES LABELS platform)
endif()
