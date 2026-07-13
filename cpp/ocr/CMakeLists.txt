add_library(doc_parser_ocr
    noop_ocr_backend.cpp
    tesseract_cli_ocr_backend.cpp
)

if(DOC_PARSER_ENABLE_ONNXRUNTIME)
    find_package(OpenCV 4.2 QUIET COMPONENTS core imgproc imgcodecs)
    if(NOT OpenCV_FOUND)
        message(FATAL_ERROR
            "OpenCV 4.2+ is required by the PaddleOCR ONNX backend. On Ubuntu/Debian run "
            "'bash scripts/setup_ubuntu_dependencies.sh' or 'sudo apt-get install -y libopencv-dev'. "
            "To build without this backend, set DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_ONNXRUNTIME=OFF."
        )
    endif()

    set(DOC_PARSER_PADDLEOCR_BASELINE_DIR
        "${PROJECT_SOURCE_DIR}/models/paddleocr/baseline"
        CACHE PATH
        "Directory containing the default PaddleOCR ONNX baseline models"
    )
    if(NOT IS_ABSOLUTE "${DOC_PARSER_PADDLEOCR_BASELINE_DIR}")
        set(DOC_PARSER_PADDLEOCR_BASELINE_DIR
            "${PROJECT_SOURCE_DIR}/${DOC_PARSER_PADDLEOCR_BASELINE_DIR}"
            CACHE PATH
            "Directory containing the default PaddleOCR ONNX baseline models"
            FORCE
        )
    endif()

    if(DOC_PARSER_AUTO_SETUP_PADDLEOCR_BASELINE
        AND (NOT EXISTS "${DOC_PARSER_PADDLEOCR_BASELINE_DIR}/det.onnx"
            OR NOT EXISTS "${DOC_PARSER_PADDLEOCR_BASELINE_DIR}/rec.onnx"
            OR NOT EXISTS "${DOC_PARSER_PADDLEOCR_BASELINE_DIR}/ppocrv5_dict.txt"))
        message(STATUS "PaddleOCR baseline models not found at ${DOC_PARSER_PADDLEOCR_BASELINE_DIR}; running scripts/setup_paddleocr_baseline.sh")
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}" -E env
                "PADDLEOCR_BASELINE_DIR=${DOC_PARSER_PADDLEOCR_BASELINE_DIR}"
                bash "${PROJECT_SOURCE_DIR}/scripts/setup_paddleocr_baseline.sh"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            RESULT_VARIABLE DOC_PARSER_PADDLEOCR_BASELINE_SETUP_RESULT
        )

        if(NOT DOC_PARSER_PADDLEOCR_BASELINE_SETUP_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download PaddleOCR baseline models. Run bash scripts/setup_paddleocr_baseline.sh manually for details, or configure with -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PADDLEOCR_BASELINE=OFF.")
        endif()
    endif()

    target_sources(doc_parser_ocr
        PRIVATE
            paddle_ocr_onnx_backend.cpp
    )
endif()

target_include_directories(doc_parser_ocr
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser_ocr
    PUBLIC
        doc_parser_document
)

if(DOC_PARSER_ENABLE_ONNXRUNTIME)
    target_link_libraries(doc_parser_ocr
        PUBLIC
            ONNXRuntime::onnxruntime
            opencv_core
            opencv_imgproc
            opencv_imgcodecs
    )

    target_include_directories(doc_parser_ocr
        SYSTEM
        PUBLIC
            ${OpenCV_INCLUDE_DIRS}
    )

    target_compile_definitions(doc_parser_ocr
        PUBLIC
            DOC_PARSER_ENABLE_ONNXRUNTIME=1
            DOC_PARSER_PADDLEOCR_BASELINE_DIR="${DOC_PARSER_PADDLEOCR_BASELINE_DIR}"
    )
endif()

doc_parser_set_common_warnings(doc_parser_ocr)
