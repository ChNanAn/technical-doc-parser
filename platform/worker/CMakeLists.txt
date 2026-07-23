add_executable(document_intelligence_worker
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
