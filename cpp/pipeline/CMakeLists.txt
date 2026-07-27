add_library(doc_parser_pipeline
    backend_registry.cpp
    document_engine.cpp
    engine_config.cpp
    pipeline_service_factory.cpp
    document_pipeline.cpp
    layout_analysis_stage.cpp
    pipeline_context.cpp
    reading_order_stage.cpp
    table_recognition_stage.cpp
    text_extraction_stage.cpp
    text_quality.cpp
)

doc_parser_set_public_includes(doc_parser_pipeline)
target_compile_definitions(doc_parser_pipeline
    INTERFACE
        $<BUILD_INTERFACE:DOC_PARSER_PADDLEOCR_BASELINE_DIR="${DOC_PARSER_PADDLEOCR_BASELINE_DIR}">
        $<BUILD_INTERFACE:DOC_PARSER_DOCLAYNET_MODEL_PATH="${DOC_PARSER_DOCLAYNET_MODEL_PATH}">
        $<BUILD_INTERFACE:DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH="${DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH}">
        $<BUILD_INTERFACE:DOC_PARSER_TABLE_DETECTION_MODEL_PATH="${DOC_PARSER_TABLE_DETECTION_MODEL_PATH}">
        $<BUILD_INTERFACE:DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH="${DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH}">
)
target_include_directories(doc_parser_pipeline
    PRIVATE
        $<BUILD_INTERFACE:${nlohmann_json_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${spdlog_SOURCE_DIR}/include>
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

if(DOC_PARSER_ENABLE_ONNXRUNTIME)
    target_compile_definitions(doc_parser_pipeline
        PRIVATE
            DOC_PARSER_ENABLE_ONNXRUNTIME=1
    )
endif()

doc_parser_set_common_warnings(doc_parser_pipeline)
