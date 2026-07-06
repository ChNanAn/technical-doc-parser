add_executable(doc_parser
    main.cpp
    ../pipeline/document_backend_factory.cpp
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
        doc_parser_export
        doc_parser_layout
        nlohmann_json::nlohmann_json
)

if(DOC_PARSER_ENABLE_PDFIUM)
    target_sources(doc_parser
        PRIVATE
            ../pipeline/pdfium_document_backend.cpp
            ../pipeline/layout_analysis_stage.cpp
            ../pipeline/text_extraction_stage.cpp
    )

    target_link_libraries(doc_parser
        PRIVATE
            doc_parser_pdf
            doc_parser_ocr
    )

    target_compile_definitions(doc_parser
        PRIVATE
            DOC_PARSER_ENABLE_PDFIUM=1
    )
endif()

if(DOC_PARSER_ENABLE_OPENCV)
    target_link_libraries(doc_parser
        PRIVATE
            doc_parser_image
    )

    target_compile_definitions(doc_parser
        PRIVATE
            DOC_PARSER_ENABLE_OPENCV=1
    )
endif()

doc_parser_set_common_warnings(doc_parser)
