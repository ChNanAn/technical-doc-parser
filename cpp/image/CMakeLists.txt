find_package(OpenCV 4.5 REQUIRED COMPONENTS core imgproc imgcodecs)

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

target_compile_options(doc_parser_image
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
