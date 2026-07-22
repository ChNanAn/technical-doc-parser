find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(
    NAME benchmark_corpus_integrity
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/validate_corpus.py
)

add_test(
    NAME benchmark_evaluator_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/test_evaluators.py
)

add_test(
    NAME benchmark_ocr_evaluator_smoke
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/evaluate_ocr.py
        --predictions ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/corpus/ocr_tesseract/ground_truth.json
        --quiet
)

add_test(
    NAME benchmark_layout_evaluator_smoke
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/evaluate_layout.py
        --predictions ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/corpus/layout_doclaynet/ground_truth.json
        --quiet
)

add_test(
    NAME benchmark_table_evaluator_smoke
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/evaluate_table.py
        --predictions ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/corpus/table_pubtables/ground_truth.json
        --quiet
)

set_tests_properties(
    benchmark_corpus_integrity
    benchmark_evaluator_tests
    benchmark_ocr_evaluator_smoke
    benchmark_layout_evaluator_smoke
    benchmark_table_evaluator_smoke
    PROPERTIES LABELS benchmark
)

add_executable(text_model_smoke
    text_model_smoke.cpp
)

target_include_directories(text_model_smoke
    PRIVATE
        ${PROJECT_SOURCE_DIR}/cpp
)

doc_parser_set_common_warnings(text_model_smoke)

add_test(NAME text_model_smoke COMMAND text_model_smoke)

add_subdirectory(unit)

if(DOC_PARSER_ENABLE_OPENCV)
    add_executable(image_preprocessor_smoke
        image_preprocessor_smoke.cpp
    )

    target_link_libraries(image_preprocessor_smoke
        PRIVATE
            doc_parser_image
    )

    doc_parser_set_common_warnings(image_preprocessor_smoke)

    add_test(NAME image_preprocessor_smoke COMMAND image_preprocessor_smoke)
endif()

if(DOC_PARSER_ENABLE_PDFIUM)
    add_test(
        NAME cli_render_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/cli_render_smoke.sh $<TARGET_FILE:document_intelligence_engine>
    )

    add_executable(pdf_library_smoke
        pdf_library_smoke.cpp
    )

    target_link_libraries(pdf_library_smoke
        PRIVATE
            doc_parser_document_source_pdf
    )

    doc_parser_set_common_warnings(pdf_library_smoke)

    add_test(NAME pdf_library_smoke COMMAND pdf_library_smoke)

    add_executable(pdf_reader_smoke
        pdf_reader_smoke.cpp
    )

    target_link_libraries(pdf_reader_smoke
        PRIVATE
            doc_parser_document_source_pdf
    )

    doc_parser_set_common_warnings(pdf_reader_smoke)

    add_test(NAME pdf_reader_smoke COMMAND pdf_reader_smoke)

    add_executable(pdf_text_extractor_smoke
        pdf_text_extractor_smoke.cpp
    )

    target_link_libraries(pdf_text_extractor_smoke
        PRIVATE
            doc_parser_document_source_pdf
    )

    doc_parser_set_common_warnings(pdf_text_extractor_smoke)

    add_test(
        NAME pdf_text_extractor_smoke
        COMMAND pdf_text_extractor_smoke ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pdfs/pdfjs-basicapi.pdf
    )

    add_executable(pdf_render_smoke
        pdf_render_smoke.cpp
    )

    target_link_libraries(pdf_render_smoke
        PRIVATE
            doc_parser_document_source_pdf
    )

    doc_parser_set_common_warnings(pdf_render_smoke)

    add_test(NAME pdf_render_smoke COMMAND pdf_render_smoke)
endif()

if(DOC_PARSER_ENABLE_ONNXRUNTIME)
    add_executable(paddle_ocr_onnx_baseline
        paddle_ocr_onnx_baseline.cpp
    )

    add_executable(funsd_ocr_eval
        funsd_ocr_eval.cpp
    )

    add_executable(doclaynet_layout_eval
        doclaynet_layout_eval.cpp
    )

    target_link_libraries(paddle_ocr_onnx_baseline
        PRIVATE
            doc_parser_ocr
    )

    target_link_libraries(funsd_ocr_eval
        PRIVATE
            doc_parser_ocr
            nlohmann_json::nlohmann_json
    )

    target_link_libraries(doclaynet_layout_eval
        PRIVATE
            doc_parser_layout
            nlohmann_json::nlohmann_json
    )

    doc_parser_set_common_warnings(paddle_ocr_onnx_baseline)
    doc_parser_set_common_warnings(funsd_ocr_eval)
    doc_parser_set_common_warnings(doclaynet_layout_eval)

    add_test(NAME paddle_ocr_onnx_baseline COMMAND paddle_ocr_onnx_baseline)
    set_tests_properties(paddle_ocr_onnx_baseline
        PROPERTIES
            SKIP_RETURN_CODE 77
            ENVIRONMENT
                "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_TEST_IMAGE=${CMAKE_CURRENT_SOURCE_DIR}/benchmark/corpus/ocr_tesseract/images/phototest.tif;DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_EXPECT_TEXT=quick"
    )

    set(DOCLAYNET_LAYOUT_PREDICTIONS "${CMAKE_CURRENT_BINARY_DIR}/doclaynet_layout_predictions.json")
    set(DOCLAYNET_LAYOUT_REPORT "${CMAKE_CURRENT_BINARY_DIR}/doclaynet_layout_report.json")
    add_test(
        NAME doclaynet_layout_benchmark
        COMMAND bash
            ${CMAKE_CURRENT_SOURCE_DIR}/run_doclaynet_layout_benchmark.sh
            $<TARGET_FILE:doclaynet_layout_eval>
            doclaynet
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/corpus/layout_doclaynet/ground_truth.json
            ${DOCLAYNET_LAYOUT_PREDICTIONS}
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/evaluate_layout.py
            ${DOCLAYNET_LAYOUT_REPORT}
            0.70
    )
    set_tests_properties(doclaynet_layout_benchmark
        PROPERTIES
            LABELS benchmark
            SKIP_RETURN_CODE 77
    )

    set(PADDLE_LAYOUT_PREDICTIONS "${CMAKE_CURRENT_BINARY_DIR}/paddle_layout_predictions.json")
    set(PADDLE_LAYOUT_REPORT "${CMAKE_CURRENT_BINARY_DIR}/paddle_layout_report.json")
    add_test(
        NAME paddle_layout_benchmark
        COMMAND bash
            ${CMAKE_CURRENT_SOURCE_DIR}/run_doclaynet_layout_benchmark.sh
            $<TARGET_FILE:doclaynet_layout_eval>
            paddle-layout
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/corpus/layout_doclaynet/ground_truth.json
            ${PADDLE_LAYOUT_PREDICTIONS}
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/evaluate_layout.py
            ${PADDLE_LAYOUT_REPORT}
            0.45
    )
    set_tests_properties(paddle_layout_benchmark
        PROPERTIES
            LABELS benchmark
            SKIP_RETURN_CODE 77
    )
endif()
