add_executable(doc_parser
    main.cpp
    ../pipeline/pipeline_context.cpp
    ../pipeline/document_pipeline.cpp
)

target_include_directories(doc_parser
    PRIVATE
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser
    PRIVATE
        CLI11::CLI11
        nlohmann_json::nlohmann_json
)

if(DOC_PARSER_ENABLE_PDFIUM)
    target_sources(doc_parser
        PRIVATE
            ../pipeline/text_extraction_stage.cpp
    )

    target_link_libraries(doc_parser
        PRIVATE
            doc_parser_pdf
    )

    target_compile_definitions(doc_parser
        PRIVATE
            DOC_PARSER_ENABLE_PDFIUM=1
    )
endif()

target_compile_options(doc_parser
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
