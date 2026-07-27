set(RELOCATED_INSTALL_PREFIX "${INSTALL_PREFIX}-relocated")
file(REMOVE_RECURSE
    "${INSTALL_PREFIX}"
    "${RELOCATED_INSTALL_PREFIX}"
    "${CONSUMER_BUILD_DIR}"
    "${CONSUMER_OUTPUT_DIR}"
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${ENGINE_BUILD_DIR}" --prefix "${INSTALL_PREFIX}"
    RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Engine installation failed with exit code ${install_result}")
endif()

file(RENAME "${INSTALL_PREFIX}" "${RELOCATED_INSTALL_PREFIX}")
set(include_root
    "${RELOCATED_INSTALL_PREFIX}/include"
)
set(expected_headers
    document_intelligence_engine/c_api.h
    document_intelligence_engine/common/diagnostic.h
    document_intelligence_engine/common/status.h
    document_intelligence_engine/document/document_block.h
    document_intelligence_engine/document/layout_model.h
    document_intelligence_engine/document/page_artifact.h
    document_intelligence_engine/document/parsed_document.h
    document_intelligence_engine/document/reading_order_model.h
    document_intelligence_engine/document/table_model.h
    document_intelligence_engine/document/text_model.h
    document_intelligence_engine/document_engine.h
    document_intelligence_engine/engine_config.h
    document_intelligence_engine/options.h
    document_intelligence_engine/provenance.h
    document_intelligence_engine/stage_observer.h
)
file(GLOB_RECURSE installed_headers
    RELATIVE "${include_root}"
    "${include_root}/*.h"
)
list(SORT expected_headers)
list(SORT installed_headers)
if(NOT installed_headers STREQUAL expected_headers)
    string(JOIN "\n  " expected_headers_text ${expected_headers})
    string(JOIN "\n  " installed_headers_text ${installed_headers})
    message(FATAL_ERROR
        "Installed public header boundary changed.\n"
        "Expected:\n  ${expected_headers_text}\n"
        "Actual:\n  ${installed_headers_text}"
    )
endif()
foreach(polluting_directory common document pipeline export layout ocr table)
    if(EXISTS "${RELOCATED_INSTALL_PREFIX}/include/${polluting_directory}")
        message(FATAL_ERROR "Installed SDK pollutes the include root with ${polluting_directory}/")
    endif()
endforeach()

set(configure_command
    "${CMAKE_COMMAND}"
    -S "${CONSUMER_SOURCE_DIR}"
    -B "${CONSUMER_BUILD_DIR}"
    "-DCMAKE_PREFIX_PATH=${RELOCATED_INSTALL_PREFIX}"
    "-DPDFium_DIR=${PDFium_DIR}"
    "-DEXPECT_INSTALLED_MODELS=${EXPECT_INSTALLED_MODELS}"
)
if(ENABLE_ONNXRUNTIME)
    list(APPEND configure_command "-DONNXRuntime_ROOT=${ONNXRuntime_ROOT}")
endif()
execute_process(
    COMMAND ${configure_command}
    RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Installed-package consumer configuration failed with exit code ${configure_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${CONSUMER_BUILD_DIR}" --parallel
    RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Installed-package consumer build failed with exit code ${build_result}")
endif()

execute_process(
    COMMAND "${CONSUMER_BUILD_DIR}/install_model_paths"
    RESULT_VARIABLE model_paths_result
)
if(NOT model_paths_result EQUAL 0)
    message(FATAL_ERROR "Relocated package model-path check failed with exit code ${model_paths_result}")
endif()

execute_process(
    COMMAND
        "${CONSUMER_BUILD_DIR}/install_consumer"
        "${TEST_PDF}"
        "${CONSUMER_OUTPUT_DIR}"
    RESULT_VARIABLE run_result
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "Installed-package consumer run failed with exit code ${run_result}")
endif()

set(c_api_consumer_command "${CONSUMER_BUILD_DIR}/install_c_consumer")
if(UNIX AND NOT APPLE)
    set(c_api_consumer_command
        "${CMAKE_COMMAND}" -E env
        "LD_LIBRARY_PATH=${PDFium_DIR}/lib:${ONNXRuntime_ROOT}/lib:$ENV{LD_LIBRARY_PATH}"
        "${CONSUMER_BUILD_DIR}/install_c_consumer"
    )
endif()
execute_process(
    COMMAND
        ${c_api_consumer_command}
        "${TEST_PDF}"
        "${CONSUMER_OUTPUT_DIR}/c-api"
    RESULT_VARIABLE c_run_result
)
if(NOT c_run_result EQUAL 0)
    message(FATAL_ERROR "Installed C API consumer run failed with exit code ${c_run_result}")
endif()
