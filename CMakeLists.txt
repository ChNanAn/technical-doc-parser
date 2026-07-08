cmake_minimum_required(VERSION 3.20)

project(document_intelligence_engine
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

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

include(CTest)

if(BUILD_TESTING)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE
    )
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

function(doc_parser_set_common_warnings target_name)
    target_compile_options(${target_name}
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
endfunction()

set(DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_PDFIUM_DESCRIPTION "Enable PDFium integration")
set(DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PDFIUM_DESCRIPTION "Download pinned PDFium during CMake configure if it is missing")
set(DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV_DESCRIPTION "Enable OpenCV image preprocessing")

set(DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_PDFIUM_DEFAULT ON)
set(DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PDFIUM_DEFAULT ON)
set(DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV_DEFAULT ON)

if(NOT DEFINED DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_PDFIUM AND DEFINED DOC_PARSER_ENABLE_PDFIUM)
    set(DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_PDFIUM_DEFAULT "${DOC_PARSER_ENABLE_PDFIUM}")
endif()

if(NOT DEFINED DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PDFIUM AND DEFINED DOC_PARSER_AUTO_SETUP_PDFIUM)
    set(DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PDFIUM_DEFAULT "${DOC_PARSER_AUTO_SETUP_PDFIUM}")
endif()

if(NOT DEFINED DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV AND DEFINED DOC_PARSER_ENABLE_OPENCV)
    set(DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV_DEFAULT "${DOC_PARSER_ENABLE_OPENCV}")
endif()

option(DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_PDFIUM
    "${DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_PDFIUM_DESCRIPTION}"
    "${DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_PDFIUM_DEFAULT}"
)
option(DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PDFIUM
    "${DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PDFIUM_DESCRIPTION}"
    "${DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PDFIUM_DEFAULT}"
)
option(DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV
    "${DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV_DESCRIPTION}"
    "${DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV_DEFAULT}"
)

set(DOC_PARSER_ENABLE_PDFIUM "${DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_PDFIUM}")
set(DOC_PARSER_AUTO_SETUP_PDFIUM "${DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PDFIUM}")
set(DOC_PARSER_ENABLE_OPENCV "${DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_OPENCV}")

add_subdirectory(cpp/common)
add_subdirectory(cpp/document)
add_subdirectory(cpp/assembly)
add_subdirectory(cpp/export)
add_subdirectory(cpp/layout)
add_subdirectory(cpp/ocr)
add_subdirectory(cpp/reading_order)
add_subdirectory(cpp/table)

if(DOC_PARSER_ENABLE_PDFIUM)
    include(PDFium)
    doc_parser_setup_pdfium()
endif()

if(DOC_PARSER_ENABLE_OPENCV)
    add_subdirectory(cpp/image)
endif()

add_subdirectory(cpp/pipeline)
add_subdirectory(cpp/backend)
add_subdirectory(cpp/app)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
