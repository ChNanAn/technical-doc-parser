add_executable(text_model_smoke
    text_model_smoke.cpp
)

target_include_directories(text_model_smoke
    PRIVATE
        ${PROJECT_SOURCE_DIR}/cpp
)

target_compile_options(text_model_smoke
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_test(NAME text_model_smoke COMMAND text_model_smoke)

if(DOC_PARSER_ENABLE_PDFIUM)
    add_test(
        NAME cli_render_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/cli_render_smoke.sh $<TARGET_FILE:doc_parser>
    )

    add_executable(pdf_library_smoke
        pdf_library_smoke.cpp
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
        pdf_reader_smoke.cpp
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

    add_executable(pdf_text_extractor_smoke
        pdf_text_extractor_smoke.cpp
    )

    target_link_libraries(pdf_text_extractor_smoke
        PRIVATE
            doc_parser_pdf
    )

    target_compile_options(pdf_text_extractor_smoke
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    add_test(
        NAME pdf_text_extractor_smoke
        COMMAND pdf_text_extractor_smoke ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pdfs/pdfjs-basicapi.pdf
    )

    add_executable(pdf_render_smoke
        pdf_render_smoke.cpp
    )

    target_link_libraries(pdf_render_smoke
        PRIVATE
            doc_parser_pdf
    )

    target_compile_options(pdf_render_smoke
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    add_test(NAME pdf_render_smoke COMMAND pdf_render_smoke)
endif()
