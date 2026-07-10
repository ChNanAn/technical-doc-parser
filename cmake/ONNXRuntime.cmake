function(doc_parser_setup_onnxruntime)
    set(DOC_PARSER_ONNXRUNTIME_VERSION "1.18.1" CACHE STRING "Pinned ONNX Runtime version")

    if(NOT DEFINED ONNXRUNTIME_ROOT OR "${ONNXRUNTIME_ROOT}" STREQUAL "")
        set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-linux-x64-${DOC_PARSER_ONNXRUNTIME_VERSION}" CACHE PATH "Path to an ONNX Runtime installation" FORCE)
    endif()

    if(NOT IS_ABSOLUTE "${ONNXRUNTIME_ROOT}")
        set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/${ONNXRUNTIME_ROOT}" CACHE PATH "Path to an ONNX Runtime installation" FORCE)
    endif()

    if(DOC_PARSER_AUTO_SETUP_ONNXRUNTIME AND (NOT EXISTS "${ONNXRUNTIME_ROOT}/include" OR NOT EXISTS "${ONNXRUNTIME_ROOT}/lib"))
        message(STATUS "ONNX Runtime not found at ${ONNXRUNTIME_ROOT}; running scripts/setup_onnxruntime.sh")
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}" -E env
                "ONNXRUNTIME_ROOT=${ONNXRUNTIME_ROOT}"
                "ONNXRUNTIME_VERSION=${DOC_PARSER_ONNXRUNTIME_VERSION}"
                bash "${CMAKE_SOURCE_DIR}/scripts/setup_onnxruntime.sh"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE DOC_PARSER_ONNXRUNTIME_SETUP_RESULT
        )

        if(NOT DOC_PARSER_ONNXRUNTIME_SETUP_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download ONNX Runtime. Run bash scripts/setup_onnxruntime.sh manually for details, or configure with -DDOCUMENT_INTELLIGENCE_ENGINE_ENABLE_ONNXRUNTIME=OFF.")
        endif()
    endif()

    find_path(ONNXRUNTIME_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h
        HINTS
            "${ONNXRUNTIME_ROOT}/include"
            "${ONNXRUNTIME_ROOT}/include/onnxruntime"
            "${ONNXRUNTIME_ROOT}/include/onnxruntime/core/session"
    )

    find_library(ONNXRUNTIME_LIBRARY
        NAMES onnxruntime
        HINTS
            "${ONNXRUNTIME_ROOT}/lib"
            "${ONNXRUNTIME_ROOT}/lib64"
    )

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(ONNXRuntime
        REQUIRED_VARS
            ONNXRUNTIME_INCLUDE_DIR
            ONNXRUNTIME_LIBRARY
    )

    if(NOT ONNXRuntime_FOUND)
        message(FATAL_ERROR
            "ONNX Runtime was enabled but not found. "
            "Set ONNXRUNTIME_ROOT to a real ONNX Runtime package directory, "
            "for example -DONNXRUNTIME_ROOT=/path/to/onnxruntime-linux-x64-${DOC_PARSER_ONNXRUNTIME_VERSION}, "
            "or configure with -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_ONNXRUNTIME=ON."
        )
    endif()

    if(NOT TARGET ONNXRuntime::onnxruntime)
        add_library(ONNXRuntime::onnxruntime UNKNOWN IMPORTED)
        set_target_properties(ONNXRuntime::onnxruntime
            PROPERTIES
                IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
        )
    endif()
endfunction()
