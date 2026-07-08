add_library(doc_parser_ocr
    noop_ocr_backend.cpp
    tesseract_cli_ocr_backend.cpp
)

if(DOC_PARSER_ENABLE_ONNXRUNTIME)
    find_package(OpenCV 4.2 REQUIRED COMPONENTS core imgproc imgcodecs)

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
    )
endif()

doc_parser_set_common_warnings(doc_parser_ocr)
