cmake_minimum_required(VERSION 3.20)

project(technical_doc_parser
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(CLI11)

enable_testing()

option(DOC_PARSER_ENABLE_PDFIUM "Enable PDFium integration" ON)

if(DOC_PARSER_ENABLE_PDFIUM)
    if(NOT DEFINED PDFium_DIR)
        set(PDFium_DIR "${CMAKE_SOURCE_DIR}/third_party/pdfium")
    endif()

    find_package(PDFium CONFIG REQUIRED)

    add_library(doc_parser_pdf
        cpp/pdf/pdf_library.cpp
        cpp/pdf/pdf_reader.cpp
    )

    target_include_directories(doc_parser_pdf
        PUBLIC
            cpp
    )

    target_link_libraries(doc_parser_pdf
        PUBLIC
            pdfium
    )

    target_compile_options(doc_parser_pdf
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
endif()

add_executable(doc_parser
    cpp/app/main.cpp
)

target_link_libraries(doc_parser
    PRIVATE
        CLI11::CLI11
)

if(DOC_PARSER_ENABLE_PDFIUM)
    target_link_libraries(doc_parser
        PRIVATE
            doc_parser_pdf
    )

    target_compile_definitions(doc_parser
        PRIVATE
            DOC_PARSER_ENABLE_PDFIUM=1
    )
endif()

target_compile_options(doc_parser
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

if(DOC_PARSER_ENABLE_PDFIUM)
    add_executable(pdf_library_smoke
        tests/pdf_library_smoke.cpp
    )

    target_link_libraries(pdf_library_smoke
        PRIVATE
            doc_parser_pdf
    )

    target_compile_options(pdf_library_smoke
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    add_test(NAME pdf_library_smoke COMMAND pdf_library_smoke)

    add_executable(pdf_reader_smoke
        tests/pdf_reader_smoke.cpp
    )

    target_link_libraries(pdf_reader_smoke
        PRIVATE
            doc_parser_pdf
    )

    target_compile_options(pdf_reader_smoke
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    add_test(NAME pdf_reader_smoke COMMAND pdf_reader_smoke)
endif()
