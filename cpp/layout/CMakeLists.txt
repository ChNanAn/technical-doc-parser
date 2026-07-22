add_library(doc_parser_layout
    layout_postprocessing.cpp
    text_layout_model_backend.cpp
)

if(DOC_PARSER_ENABLE_ONNXRUNTIME)
    find_package(OpenCV 4.2 REQUIRED COMPONENTS core imgproc imgcodecs)

    set(DOC_PARSER_DOCLAYNET_MODEL_PATH
        "${PROJECT_SOURCE_DIR}/models/layout/doclaynet/model.onnx"
        CACHE FILEPATH
        "Path to the DocLayNet RF-DETR ONNX model"
    )
    if(NOT IS_ABSOLUTE "${DOC_PARSER_DOCLAYNET_MODEL_PATH}")
        set(DOC_PARSER_DOCLAYNET_MODEL_PATH
            "${PROJECT_SOURCE_DIR}/${DOC_PARSER_DOCLAYNET_MODEL_PATH}"
            CACHE FILEPATH
            "Path to the DocLayNet RF-DETR ONNX model"
            FORCE
        )
    endif()

    set(DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH
        "${PROJECT_SOURCE_DIR}/models/layout/paddle/pp-doclayout-v3.onnx"
        CACHE FILEPATH
        "Path to the Paddle PP-DocLayoutV3 ONNX model"
    )
    if(NOT IS_ABSOLUTE "${DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH}")
        set(DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH
            "${PROJECT_SOURCE_DIR}/${DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH}"
            CACHE FILEPATH
            "Path to the Paddle PP-DocLayoutV3 ONNX model"
            FORCE
        )
    endif()

    if(DOC_PARSER_AUTO_SETUP_DOCLAYNET_LAYOUT AND NOT EXISTS "${DOC_PARSER_DOCLAYNET_MODEL_PATH}")
        message(STATUS "DocLayNet model not found at ${DOC_PARSER_DOCLAYNET_MODEL_PATH}; running scripts/setup_doclaynet_layout.sh")
        get_filename_component(DOC_PARSER_DOCLAYNET_MODEL_DIR "${DOC_PARSER_DOCLAYNET_MODEL_PATH}" DIRECTORY)
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}" -E env
                "DOCLAYNET_MODEL_DIR=${DOC_PARSER_DOCLAYNET_MODEL_DIR}"
                "DOCLAYNET_MODEL=${DOC_PARSER_DOCLAYNET_MODEL_PATH}"
                bash "${PROJECT_SOURCE_DIR}/scripts/setup_doclaynet_layout.sh"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            RESULT_VARIABLE DOC_PARSER_DOCLAYNET_SETUP_RESULT
        )
        if(NOT DOC_PARSER_DOCLAYNET_SETUP_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download the DocLayNet layout model. Run bash scripts/setup_doclaynet_layout.sh manually, disable DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_DOCLAYNET_LAYOUT, or disable ONNX Runtime.")
        endif()
    endif()

    if(DOC_PARSER_AUTO_SETUP_PADDLE_LAYOUT AND NOT EXISTS "${DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH}")
        message(STATUS "Paddle Layout model not found at ${DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH}; running scripts/setup_paddle_layout.sh")
        get_filename_component(DOC_PARSER_PADDLE_LAYOUT_MODEL_DIR "${DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH}" DIRECTORY)
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}" -E env
                "PADDLE_LAYOUT_DIR=${DOC_PARSER_PADDLE_LAYOUT_MODEL_DIR}"
                "PADDLE_LAYOUT_MODEL=${DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH}"
                bash "${PROJECT_SOURCE_DIR}/scripts/setup_paddle_layout.sh"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            RESULT_VARIABLE DOC_PARSER_PADDLE_LAYOUT_SETUP_RESULT
        )
        if(NOT DOC_PARSER_PADDLE_LAYOUT_SETUP_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download the Paddle Layout model. Run bash scripts/setup_paddle_layout.sh manually, disable DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PADDLE_LAYOUT, or disable ONNX Runtime.")
        endif()
    endif()

    target_sources(doc_parser_layout PRIVATE doclaynet_onnx_backend.cpp paddle_doclayout_onnx_backend.cpp)
endif()

target_include_directories(doc_parser_layout
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser_layout
    PUBLIC
        doc_parser_document
)

if(DOC_PARSER_ENABLE_ONNXRUNTIME)
    target_link_libraries(doc_parser_layout
        PUBLIC
            ONNXRuntime::onnxruntime
            opencv_core
            opencv_imgproc
            opencv_imgcodecs
    )
    target_include_directories(doc_parser_layout SYSTEM PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(doc_parser_layout
        PUBLIC
            DOC_PARSER_ENABLE_ONNXRUNTIME=1
            DOC_PARSER_DOCLAYNET_MODEL_PATH="${DOC_PARSER_DOCLAYNET_MODEL_PATH}"
            DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH="${DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH}"
    )
else()
    target_compile_definitions(doc_parser_layout PUBLIC DOC_PARSER_ENABLE_ONNXRUNTIME=0)
endif()

doc_parser_set_common_warnings(doc_parser_layout)
