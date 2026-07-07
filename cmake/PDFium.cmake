function(doc_parser_setup_pdfium)
    if(NOT DEFINED PDFium_DIR)
        set(PDFium_DIR "${CMAKE_SOURCE_DIR}/third_party/pdfium" CACHE PATH "Path to the PDFium package")
    endif()

    if(NOT IS_ABSOLUTE "${PDFium_DIR}")
        set(PDFium_DIR "${CMAKE_SOURCE_DIR}/${PDFium_DIR}" CACHE PATH "Path to the PDFium package" FORCE)
    endif()

    set(DOC_PARSER_PDFIUM_CONFIG "${PDFium_DIR}/PDFiumConfig.cmake")

    if(DOC_PARSER_AUTO_SETUP_PDFIUM AND NOT EXISTS "${DOC_PARSER_PDFIUM_CONFIG}")
        message(STATUS "PDFium not found at ${PDFium_DIR}; running scripts/setup_pdfium.sh")
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}" -E env
                "PDFIUM_DIR=${PDFium_DIR}"
                bash "${CMAKE_SOURCE_DIR}/scripts/setup_pdfium.sh"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE DOC_PARSER_PDFIUM_SETUP_RESULT
        )

        if(NOT DOC_PARSER_PDFIUM_SETUP_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download PDFium. Run bash scripts/setup_pdfium.sh manually for details, or configure with -DDOCUMENT_INTELLIGENCE_ENGINE_ENABLE_PDFIUM=OFF.")
        endif()
    endif()

    if(NOT EXISTS "${DOC_PARSER_PDFIUM_CONFIG}")
        message(FATAL_ERROR "PDFium package config not found at ${DOC_PARSER_PDFIUM_CONFIG}. Run bash scripts/setup_pdfium.sh, pass -DPDFium_DIR=/path/to/pdfium, or enable -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PDFIUM=ON.")
    endif()

    find_package(PDFium CONFIG REQUIRED)
endfunction()
