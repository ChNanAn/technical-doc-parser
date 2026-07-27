find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(TARGET document_intelligence_worker)
    add_test(
        NAME platform_worker_environment_config
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/worker_environment_config_test.py
            --worker $<TARGET_FILE:document_intelligence_worker>
    )
    set_tests_properties(platform_worker_environment_config PROPERTIES LABELS platform)
endif()

if(DOC_PARSER_ENABLE_PDFIUM)
    add_test(
        NAME cmake_install_consumer
        COMMAND
            ${CMAKE_COMMAND}
            "-DENGINE_BUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DINSTALL_PREFIX=${PROJECT_BINARY_DIR}/install-consumer-prefix"
            "-DCONSUMER_SOURCE_DIR=${PROJECT_SOURCE_DIR}/tests/install_consumer"
            "-DCONSUMER_BUILD_DIR=${PROJECT_BINARY_DIR}/install-consumer-build"
            "-DCONSUMER_OUTPUT_DIR=${PROJECT_BINARY_DIR}/install-consumer-output"
            "-DPDFium_DIR=${PDFium_DIR}"
            "-DENABLE_ONNXRUNTIME=${DOC_PARSER_ENABLE_ONNXRUNTIME}"
            "-DONNXRuntime_ROOT=${ONNXRuntime_ROOT}"
            "-DTEST_PDF=${PROJECT_SOURCE_DIR}/tests/fixtures/pdfs/pdfjs-basicapi.pdf"
            -P "${PROJECT_SOURCE_DIR}/tests/install_consumer/run.cmake"
    )
    set_tests_properties(cmake_install_consumer PROPERTIES LABELS packaging)
endif()

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
        --maximum-cer 0.0
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
        --maximum-table-text-cer 0.0
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
    if(DOCUMENT_INTELLIGENCE_ENGINE_REQUIRE_BENCHMARK_MODELS)
        set(REQUIRED_BENCHMARK_MODEL_FILES
            "${DOC_PARSER_PADDLEOCR_BASELINE_DIR}/det.onnx"
            "${DOC_PARSER_PADDLEOCR_BASELINE_DIR}/rec.onnx"
            "${DOC_PARSER_PADDLEOCR_BASELINE_DIR}/ppocrv5_dict.txt"
            "${DOC_PARSER_DOCLAYNET_MODEL_PATH}"
            "${DOC_PARSER_PADDLE_LAYOUT_MODEL_PATH}"
            "${DOC_PARSER_TABLE_DETECTION_MODEL_PATH}"
            "${DOC_PARSER_TABLE_STRUCTURE_MODEL_PATH}"
        )
        foreach(model_file IN LISTS REQUIRED_BENCHMARK_MODEL_FILES)
            if(NOT EXISTS "${model_file}")
                message(FATAL_ERROR "Required benchmark model is missing: ${model_file}")
            endif()
            file(SIZE "${model_file}" model_size)
            if(model_size EQUAL 0)
                message(FATAL_ERROR "Required benchmark model is empty: ${model_file}")
            endif()
        endforeach()
    endif()

    add_executable(paddle_ocr_onnx_baseline
        paddle_ocr_onnx_baseline.cpp
    )

    add_executable(paddle_ocr_eval
        paddle_ocr_eval.cpp
    )

    add_executable(pipeline_quality_eval
        pipeline_quality_eval.cpp
    )

    add_executable(olmocr_bench_runner
        olmocr_bench_runner.cpp
    )

    add_executable(funsd_ocr_eval
        funsd_ocr_eval.cpp
    )

    add_executable(doclaynet_layout_eval
        doclaynet_layout_eval.cpp
    )

    add_executable(pubtables_table_eval
        pubtables_table_eval.cpp
    )

    target_link_libraries(paddle_ocr_onnx_baseline
        PRIVATE
            doc_parser_ocr
    )

    target_link_libraries(paddle_ocr_eval
        PRIVATE
            doc_parser_ocr
            nlohmann_json::nlohmann_json
    )

    target_link_libraries(pipeline_quality_eval
        PRIVATE
            doc_parser_pipeline
            nlohmann_json::nlohmann_json
    )

    target_link_libraries(olmocr_bench_runner
        PRIVATE
            doc_parser_export
            doc_parser_pipeline
            nlohmann_json::nlohmann_json
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

    target_link_libraries(pubtables_table_eval
        PRIVATE
            doc_parser_ocr
            doc_parser_table
            nlohmann_json::nlohmann_json
    )

    doc_parser_set_common_warnings(paddle_ocr_onnx_baseline)
    doc_parser_set_common_warnings(paddle_ocr_eval)
    doc_parser_set_common_warnings(pipeline_quality_eval)
    doc_parser_set_common_warnings(olmocr_bench_runner)
    doc_parser_set_common_warnings(funsd_ocr_eval)
    doc_parser_set_common_warnings(doclaynet_layout_eval)
    doc_parser_set_common_warnings(pubtables_table_eval)

    add_test(NAME paddle_ocr_onnx_baseline COMMAND paddle_ocr_onnx_baseline)
    set_tests_properties(paddle_ocr_onnx_baseline
        PROPERTIES
            ENVIRONMENT
                "DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_TEST_IMAGE=${CMAKE_CURRENT_SOURCE_DIR}/benchmark/corpus/ocr_tesseract/images/phototest.tif;DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_EXPECT_TEXT=quick"
    )

    set(PADDLE_OCR_PREDICTIONS "${CMAKE_CURRENT_BINARY_DIR}/paddle_ocr_predictions.json")
    set(PADDLE_OCR_REPORT "${CMAKE_CURRENT_BINARY_DIR}/paddle_ocr_report.json")
    add_test(
        NAME paddle_ocr_benchmark
        COMMAND bash
            ${CMAKE_CURRENT_SOURCE_DIR}/run_paddle_ocr_benchmark.sh
            $<TARGET_FILE:paddle_ocr_eval>
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/corpus/ocr_tesseract/ground_truth.json
            ${PADDLE_OCR_PREDICTIONS}
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/evaluate_ocr.py
            ${PADDLE_OCR_REPORT}
            0.70
    )
    set_tests_properties(paddle_ocr_benchmark PROPERTIES LABELS benchmark)

    if(DOCUMENT_INTELLIGENCE_ENGINE_ENABLE_PIPELINE_BENCHMARK)
        set(PIPELINE_QUALITY_ROOT "${PROJECT_SOURCE_DIR}/data/raw/quality_baseline/inputs")
        set(PIPELINE_QUALITY_PREDICTIONS "${CMAKE_CURRENT_BINARY_DIR}/pipeline_quality_predictions.json")
        set(PIPELINE_QUALITY_REPORT "${CMAKE_CURRENT_BINARY_DIR}/pipeline_quality_report.json")
        add_test(
            NAME pipeline_quality_benchmark
            COMMAND bash
                ${CMAKE_CURRENT_SOURCE_DIR}/run_pipeline_quality_benchmark.sh
                $<TARGET_FILE:pipeline_quality_eval>
                ${PIPELINE_QUALITY_ROOT}/prepared_manifest.json
                ${PIPELINE_QUALITY_ROOT}
                ${CMAKE_CURRENT_BINARY_DIR}/pipeline-quality-work
                ${PIPELINE_QUALITY_PREDICTIONS}
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/evaluate_pipeline.py
                ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/corpus/pipeline_quality/ground_truth.json
                ${PIPELINE_QUALITY_REPORT}
                0.88
                0.84
                0.92
                0.16
        )
        set_tests_properties(pipeline_quality_benchmark PROPERTIES LABELS "benchmark;pipeline")

        set(PIPELINE_BLOCK_TYPE_ROOT "${PIPELINE_QUALITY_ROOT}/pipeline_doclaynet")
        set(PIPELINE_BLOCK_TYPE_PREDICTIONS "${CMAKE_CURRENT_BINARY_DIR}/pipeline_block_type_predictions.json")
        set(PIPELINE_BLOCK_TYPE_REPORT "${CMAKE_CURRENT_BINARY_DIR}/pipeline_block_type_report.json")
        add_test(
            NAME pipeline_block_type_benchmark
            COMMAND bash
                ${CMAKE_CURRENT_SOURCE_DIR}/run_pipeline_block_type_benchmark.sh
                $<TARGET_FILE:pipeline_quality_eval>
                ${PIPELINE_BLOCK_TYPE_ROOT}/prepared_manifest.json
                ${PIPELINE_BLOCK_TYPE_ROOT}
                ${CMAKE_CURRENT_BINARY_DIR}/pipeline-block-type-work
                ${PIPELINE_BLOCK_TYPE_PREDICTIONS}
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/evaluate_layout.py
                ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/corpus/layout_doclaynet/ground_truth.json
                ${PIPELINE_BLOCK_TYPE_REPORT}
                0.75
        )
        set_tests_properties(pipeline_block_type_benchmark PROPERTIES LABELS "benchmark;pipeline")
    endif()

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
    )
    if(NOT DOCUMENT_INTELLIGENCE_ENGINE_REQUIRE_BENCHMARK_MODELS)
        set_tests_properties(doclaynet_layout_benchmark PROPERTIES SKIP_RETURN_CODE 77)
    endif()

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
    )
    if(NOT DOCUMENT_INTELLIGENCE_ENGINE_REQUIRE_BENCHMARK_MODELS)
        set_tests_properties(paddle_layout_benchmark PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    set(PUBTABLES_TABLE_PREDICTIONS "${CMAKE_CURRENT_BINARY_DIR}/pubtables_table_predictions.json")
    set(PUBTABLES_TABLE_REPORT "${CMAKE_CURRENT_BINARY_DIR}/pubtables_table_report.json")
    add_test(
        NAME pubtables_table_benchmark
        COMMAND bash
            ${CMAKE_CURRENT_SOURCE_DIR}/run_pubtables_table_benchmark.sh
            $<TARGET_FILE:pubtables_table_eval>
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/corpus/table_pubtables/ground_truth.json
            ${PUBTABLES_TABLE_PREDICTIONS}
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/evaluate_table.py
            ${PUBTABLES_TABLE_REPORT}
            0.95
            0.08
    )
    set_tests_properties(pubtables_table_benchmark
        PROPERTIES
            LABELS benchmark
    )
    if(NOT DOCUMENT_INTELLIGENCE_ENGINE_REQUIRE_BENCHMARK_MODELS)
        set_tests_properties(pubtables_table_benchmark PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
