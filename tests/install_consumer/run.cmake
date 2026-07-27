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
set(namespaced_include
    "${RELOCATED_INSTALL_PREFIX}/include/document_intelligence_engine/pipeline/document_engine.h"
)
if(NOT EXISTS "${namespaced_include}")
    message(FATAL_ERROR "Installed SDK header is missing from its namespace: ${namespaced_include}")
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
