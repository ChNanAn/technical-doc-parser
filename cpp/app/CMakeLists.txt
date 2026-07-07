add_executable(document_intelligence_engine
    main.cpp
)

target_include_directories(document_intelligence_engine
    PRIVATE
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(document_intelligence_engine
    PRIVATE
        CLI11::CLI11
        doc_parser_pipeline
        doc_parser_backend_document
        nlohmann_json::nlohmann_json
)

doc_parser_set_common_warnings(document_intelligence_engine)
