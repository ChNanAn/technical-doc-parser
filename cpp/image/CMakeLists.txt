find_package(OpenCV 4.2 QUIET COMPONENTS core imgproc imgcodecs)
if(NOT OpenCV_FOUND)
    message(FATAL_ERROR
        "OpenCV 4.2+ is required for page image access and preprocessing. On Ubuntu/Debian run "
        "'bash scripts/setup_ubuntu_dependencies.sh' or 'sudo apt-get install -y libopencv-dev'. "
        "To build without OpenCV, set DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV=OFF "
        "and DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_ONNXRUNTIME=OFF."
    )
endif()

message(STATUS "OpenCV version: ${OpenCV_VERSION}")

add_library(doc_parser_image
    image_preprocessor.cpp
    oriented_page_view.cpp
    page_image_cache.cpp
)

doc_parser_set_public_includes(doc_parser_image)

target_link_libraries(doc_parser_image
    PUBLIC
        doc_parser_document
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
)

doc_parser_set_common_warnings(doc_parser_image)
