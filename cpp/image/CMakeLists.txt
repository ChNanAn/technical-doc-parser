find_package(OpenCV 4.2 QUIET COMPONENTS core imgproc imgcodecs)
if(NOT OpenCV_FOUND)
    message(FATAL_ERROR
        "OpenCV 4.2+ is required for image preprocessing. On Ubuntu/Debian run "
        "'bash scripts/setup_ubuntu_dependencies.sh' or 'sudo apt-get install -y libopencv-dev'. "
        "To build without image preprocessing, set DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV=OFF."
    )
endif()

message(STATUS "OpenCV version: ${OpenCV_VERSION}")

add_library(doc_parser_image
    image_preprocessor.cpp
)

target_include_directories(doc_parser_image
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_include_directories(doc_parser_image
    SYSTEM
    PUBLIC
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(doc_parser_image
    PUBLIC
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
)

doc_parser_set_common_warnings(doc_parser_image)
