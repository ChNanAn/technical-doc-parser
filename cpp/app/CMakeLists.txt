add_executable(doc_parser
    main.cpp
)

target_include_directories(doc_parser
    PRIVATE
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser
    PRIVATE
        CLI11::CLI11
        doc_parser_pipeline
        nlohmann_json::nlohmann_json
)

doc_parser_set_common_warnings(doc_parser)
