find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS
        "${ONNXRuntime_ROOT}/include"
        "${ONNXRuntime_ROOT}/include/onnxruntime"
        "${ONNXRuntime_ROOT}/include/onnxruntime/core/session"
)

find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    HINTS
        "${ONNXRuntime_ROOT}/lib"
        "${ONNXRuntime_ROOT}/lib64"
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
