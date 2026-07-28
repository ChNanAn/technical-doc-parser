add_library(document_intelligence_engine_sdk
    src/document_engine.cpp
    src/engine_config.cpp
)

target_include_directories(document_intelligence_engine_sdk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/cpp>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/document_intelligence_engine>
)

doc_parser_set_common_warnings(document_intelligence_engine_sdk)
