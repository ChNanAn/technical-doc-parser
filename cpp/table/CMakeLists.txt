add_library(doc_parser_table
    text_table_structure_backend.cpp
)

if(DOC_PARSER_ENABLE_ONNXRUNTIME)
    find_package(OpenCV 4.2 REQUIRED COMPONENTS core imgproc imgcodecs)

    set(DOC_PARSER_TABLE_DETECTION_MODEL_PATH
        "${PROJECT_SOURCE_DIR}/models/table/table-transformer/detection.onnx"
        CACHE FILEPATH
        "Path to the Table Transformer detection ONNX model"
    )
    set(DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH
        "${PROJECT_SOURCE_DIR}/models/table/table-transformer/structure.onnx"
        CACHE FILEPATH
        "Path to the Table Transformer structure ONNX model"
    )
    foreach(model_variable DOC_PARSER_TABLE_DETECTION_MODEL_PATH DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH)
        if(NOT IS_ABSOLUTE "${${model_variable}}")
            set(${model_variable}
                "${PROJECT_SOURCE_DIR}/${${model_variable}}"
                CACHE FILEPATH
                "Resolved Table Transformer model path"
                FORCE
            )
        endif()
    endforeach()

    if(DOC_PARSER_AUTO_SETUP_TABLE_TRANSFORMER AND
       (NOT EXISTS "${DOC_PARSER_TABLE_DETECTION_MODEL_PATH}" OR
        NOT EXISTS "${DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH}"))
        message(STATUS "Table Transformer models are missing; running scripts/setup_table_transformer.sh")
        get_filename_component(DOC_PARSER_TABLE_TRANSFORMER_DIR "${DOC_PARSER_TABLE_DETECTION_MODEL_PATH}" DIRECTORY)
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}" -E env
                "TABLE_TRANSFORMER_DIR=${DOC_PARSER_TABLE_TRANSFORMER_DIR}"
                "TABLE_DETECTION_MODEL=${DOC_PARSER_TABLE_DETECTION_MODEL_PATH}"
                "TABLE_STRUCTURE_MODEL=${DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH}"
                bash "${PROJECT_SOURCE_DIR}/scripts/setup_table_transformer.sh"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            RESULT_VARIABLE DOC_PARSER_TABLE_TRANSFORMER_SETUP_RESULT
        )
        if(NOT DOC_PARSER_TABLE_TRANSFORMER_SETUP_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download Table Transformer models. Run bash scripts/setup_table_transformer.sh manually, disable DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_TABLE_TRANSFORMER, or disable ONNX Runtime.")
        endif()
    endif()

    target_sources(doc_parser_table PRIVATE table_transformer_onnx_backend.cpp)
endif()

target_include_directories(doc_parser_table
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser_table
    PUBLIC
        doc_parser_document
)

if(DOC_PARSER_ENABLE_ONNXRUNTIME)
    target_link_libraries(doc_parser_table
        PUBLIC
            ONNXRuntime::onnxruntime
            opencv_core
            opencv_imgproc
            opencv_imgcodecs
    )
    target_include_directories(doc_parser_table SYSTEM PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(doc_parser_table
        PUBLIC
            DOC_PARSER_ENABLE_ONNXRUNTIME=1
            DOC_PARSER_TABLE_DETECTION_MODEL_PATH="${DOC_PARSER_TABLE_DETECTION_MODEL_PATH}"
            DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH="${DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH}"
    )
else()
    target_compile_definitions(doc_parser_table PUBLIC DOC_PARSER_ENABLE_ONNXRUNTIME=0)
endif()

doc_parser_set_common_warnings(doc_parser_table)
