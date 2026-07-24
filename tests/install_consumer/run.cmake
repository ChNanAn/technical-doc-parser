file(REMOVE_RECURSE "${INSTALL_PREFIX}" "${CONSUMER_BUILD_DIR}" "${CONSUMER_OUTPUT_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${ENGINE_BUILD_DIR}" --prefix "${INSTALL_PREFIX}"
    RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Engine installation failed with exit code ${install_result}")
endif()

set(configure_command
    "${CMAKE_COMMAND}"
    -S "${CONSUMER_SOURCE_DIR}"
    -B "${CONSUMER_BUILD_DIR}"
    "-DCMAKE_PREFIX_PATH=${INSTALL_PREFIX}"
    "-DPDFium_DIR=${PDFium_DIR}"
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
    COMMAND
        "${CONSUMER_BUILD_DIR}/install_consumer"
        "${TEST_PDF}"
        "${CONSUMER_OUTPUT_DIR}"
    RESULT_VARIABLE run_result
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "Installed-package consumer run failed with exit code ${run_result}")
endif()
