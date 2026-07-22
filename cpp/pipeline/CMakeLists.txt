add_library(doc_parser_pipeline
    backend_registry.cpp
    pipeline_service_factory.cpp
    document_pipeline.cpp
    layout_analysis_stage.cpp
    pipeline_context.cpp
    reading_order_stage.cpp
    table_recognition_stage.cpp
    text_extraction_stage.cpp
    text_quality.cpp
)

target_include_directories(doc_parser_pipeline
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser_pipeline
    PUBLIC
        doc_parser_assembly
        doc_parser_common
        doc_parser_document
        doc_parser_document_source
        doc_parser_export
        doc_parser_layout
        doc_parser_ocr
        doc_parser_reading_order
        doc_parser_table
        spdlog::spdlog
    PRIVATE
        nlohmann_json::nlohmann_json
)

if(DOC_PARSER_ENABLE_OPENCV)
    target_link_libraries(doc_parser_pipeline
        PUBLIC
            doc_parser_image
    )

    target_compile_definitions(doc_parser_pipeline
        PRIVATE
            DOC_PARSER_ENABLE_OPENCV=1
    )
endif()

doc_parser_set_common_warnings(doc_parser_pipeline)
