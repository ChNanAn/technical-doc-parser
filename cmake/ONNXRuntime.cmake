function(doc_parser_setup_onnxruntime)
    set(ONNXRUNTIME_ROOT "" CACHE PATH "Path to an ONNX Runtime installation")

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

    if(ONNXRuntime_FOUND AND NOT TARGET ONNXRuntime::onnxruntime)
        add_library(ONNXRuntime::onnxruntime UNKNOWN IMPORTED)
        set_target_properties(ONNXRuntime::onnxruntime
            PROPERTIES
                IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
        )
    endif()
endfunction()
