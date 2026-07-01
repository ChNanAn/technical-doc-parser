add_library(doc_parser_pdf
    pdf_library.cpp
    pdf_page_renderer.cpp
    pdf_reader.cpp
    pdf_text_extractor.cpp
    pdfium_runtime.cpp
    render_service.cpp
    text_service.cpp
)

target_include_directories(doc_parser_pdf
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_include_directories(doc_parser_pdf
    SYSTEM
    PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party/stb
)

target_link_libraries(doc_parser_pdf
    PUBLIC
        pdfium
)

target_compile_options(doc_parser_pdf
    PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
