function(doc_parser_setup_onnxruntime)
    set(DOC_PARSER_ONNXRUNTIME_VERSION "1.18.1" CACHE STRING "Pinned ONNX Runtime version")

    if(DEFINED ONNXRUNTIME_ROOT AND NOT "${ONNXRUNTIME_ROOT}" STREQUAL "")
        if(NOT DEFINED ONNXRuntime_ROOT OR "${ONNXRuntime_ROOT}" STREQUAL "")
            set(ONNXRuntime_ROOT "${ONNXRUNTIME_ROOT}" CACHE PATH "Path to an ONNX Runtime installation" FORCE)
        endif()
        unset(ONNXRUNTIME_ROOT CACHE)
    endif()

    if(NOT DEFINED ONNXRuntime_ROOT OR "${ONNXRuntime_ROOT}" STREQUAL "")
        set(ONNXRuntime_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-linux-x64-${DOC_PARSER_ONNXRUNTIME_VERSION}" CACHE PATH "Path to an ONNX Runtime installation" FORCE)
    endif()

    if(NOT IS_ABSOLUTE "${ONNXRuntime_ROOT}")
        set(ONNXRuntime_ROOT "${CMAKE_SOURCE_DIR}/${ONNXRuntime_ROOT}" CACHE PATH "Path to an ONNX Runtime installation" FORCE)
    endif()

    if(DOC_PARSER_AUTO_SETUP_ONNXRUNTIME AND (NOT EXISTS "${ONNXRuntime_ROOT}/include" OR NOT EXISTS "${ONNXRuntime_ROOT}/lib"))
        message(STATUS "ONNX Runtime not found at ${ONNXRuntime_ROOT}; running scripts/setup_onnxruntime.sh")
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}" -E env
                "ONNXRUNTIME_ROOT=${ONNXRuntime_ROOT}"
                "ONNXRUNTIME_VERSION=${DOC_PARSER_ONNXRUNTIME_VERSION}"
                bash "${CMAKE_SOURCE_DIR}/scripts/setup_onnxruntime.sh"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE DOC_PARSER_ONNXRUNTIME_SETUP_RESULT
        )

        if(NOT DOC_PARSER_ONNXRUNTIME_SETUP_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download ONNX Runtime. Run bash scripts/setup_onnxruntime.sh manually for details, or configure with -DDOCUMENT_INTELLIGENCE_ENGINE_ENABLE_ONNXRUNTIME=OFF.")
        endif()
    endif()

    find_package(ONNXRuntime REQUIRED MODULE)
endfunction()
