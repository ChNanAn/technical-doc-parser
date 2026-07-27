add_executable(document_intelligence_engine
    main.cpp
)

set_target_properties(document_intelligence_engine PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
)

target_include_directories(document_intelligence_engine
    PRIVATE
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(document_intelligence_engine
    PRIVATE
        CLI11::CLI11
        doc_parser_pipeline
        spdlog::spdlog
)

doc_parser_set_common_warnings(document_intelligence_engine)

install(TARGETS document_intelligence_engine
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Runtime
)
