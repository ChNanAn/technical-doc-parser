cmake_minimum_required(VERSION 3.20)

project(technical_doc_parser
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(CLI11)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

enable_testing()

function(doc_parser_set_common_warnings target_name)
    target_compile_options(${target_name}
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
endfunction()

option(DOC_PARSER_ENABLE_PDFIUM "Enable PDFium integration" ON)
option(DOC_PARSER_AUTO_SETUP_PDFIUM "Download pinned PDFium during CMake configure if it is missing" ON)
option(DOC_PARSER_ENABLE_OPENCV "Enable OpenCV image preprocessing" ON)

if(DOC_PARSER_ENABLE_PDFIUM)
    include(PDFium)
    doc_parser_setup_pdfium()
    add_subdirectory(cpp/pdf)
endif()

if(DOC_PARSER_ENABLE_OPENCV)
    add_subdirectory(cpp/image)
endif()

add_subdirectory(cpp/app)
add_subdirectory(tests)
