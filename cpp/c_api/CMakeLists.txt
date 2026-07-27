add_library(document_intelligence_engine_c SHARED
    document_intelligence_engine.cpp
)

target_include_directories(document_intelligence_engine_c
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/cpp>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/document_intelligence_engine>
)
target_compile_definitions(document_intelligence_engine_c
    PRIVATE
        DIE_C_API_BUILD=1
        DIE_ENGINE_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(document_intelligence_engine_c
    PRIVATE
        doc_parser_export
        document_intelligence_engine_sdk
        $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>
)
# Consumers link only the shared C API, but their linker still needs to locate
# its external shared-library dependencies after the SDK has been relocated.
if(DOC_PARSER_ENABLE_PDFIUM)
    target_link_libraries(document_intelligence_engine_c
        INTERFACE
            $<LINK_ONLY:pdfium>
    )
endif()
if(DOC_PARSER_ENABLE_OPENCV)
    target_link_libraries(document_intelligence_engine_c
        INTERFACE
            $<LINK_ONLY:opencv_core>
            $<LINK_ONLY:opencv_imgproc>
            $<LINK_ONLY:opencv_imgcodecs>
    )
endif()
if(DOC_PARSER_ENABLE_ONNXRUNTIME)
    target_link_libraries(document_intelligence_engine_c
        INTERFACE
            $<LINK_ONLY:ONNXRuntime::onnxruntime>
    )
endif()
set_target_properties(document_intelligence_engine_c PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_target_properties(document_intelligence_engine_c PROPERTIES
        INSTALL_RPATH "$ORIGIN"
    )
    set(DOCUMENT_INTELLIGENCE_ENGINE_C_API_VERSION_SCRIPT
        "${CMAKE_CURRENT_SOURCE_DIR}/document_intelligence_engine.map"
    )
    target_link_options(document_intelligence_engine_c
        PRIVATE
            "LINKER:--no-undefined"
            "LINKER:--version-script=${DOCUMENT_INTELLIGENCE_ENGINE_C_API_VERSION_SCRIPT}"
    )
    set_property(TARGET document_intelligence_engine_c APPEND PROPERTY
        LINK_DEPENDS "${DOCUMENT_INTELLIGENCE_ENGINE_C_API_VERSION_SCRIPT}"
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(document_intelligence_engine_c PRIVATE "-Wl,--no-undefined")
endif()

doc_parser_set_common_warnings(document_intelligence_engine_c)
