if(NOT DEFINED NM_EXECUTABLE OR NM_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "NM_EXECUTABLE is required")
endif()
if(NOT DEFINED C_API_LIBRARY OR NOT EXISTS "${C_API_LIBRARY}")
    message(FATAL_ERROR "C_API_LIBRARY does not exist: ${C_API_LIBRARY}")
endif()

execute_process(
    COMMAND "${NM_EXECUTABLE}" -D --defined-only "${C_API_LIBRARY}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE nm_output
    ERROR_VARIABLE nm_error
)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "Unable to inspect C API exports: ${nm_error}")
endif()

set(expected_exports
    die_abi_version
    die_document_destroy
    die_document_json
    die_document_json_size
    die_engine_create
    die_engine_destroy
    die_engine_get_state
    die_engine_parse
    die_engine_version
    die_error_code
    die_error_destroy
    die_error_message
    die_error_result
    die_error_retryable
    die_error_stage
)

string(REPLACE "\r\n" "\n" nm_output "${nm_output}")
string(REPLACE "\n" ";" nm_lines "${nm_output}")
set(actual_exports)
set(unexpected_exports)
foreach(line IN LISTS nm_lines)
    if(line MATCHES "[ \t]([^ \t]+)$")
        set(symbol "${CMAKE_MATCH_1}")
        string(REGEX REPLACE "@@?DIE_1\\.0$" "" symbol "${symbol}")
        if(symbol STREQUAL "DIE_1.0")
            continue()
        endif()
        if(symbol MATCHES "^die_")
            list(APPEND actual_exports "${symbol}")
        else()
            list(APPEND unexpected_exports "${symbol}")
        endif()
    endif()
endforeach()

list(REMOVE_DUPLICATES actual_exports)
list(SORT actual_exports)
list(SORT expected_exports)
if(unexpected_exports)
    list(REMOVE_DUPLICATES unexpected_exports)
    list(SORT unexpected_exports)
    list(JOIN unexpected_exports "\n  " unexpected_text)
    message(FATAL_ERROR
        "C API shared library exposes non-ABI symbols:\n  ${unexpected_text}"
    )
endif()
if(NOT actual_exports STREQUAL expected_exports)
    list(JOIN expected_exports "\n  " expected_text)
    list(JOIN actual_exports "\n  " actual_text)
    message(FATAL_ERROR
        "C API export set does not match ABI v1.\n"
        "Expected:\n  ${expected_text}\n"
        "Actual:\n  ${actual_text}"
    )
endif()
