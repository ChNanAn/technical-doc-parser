#ifndef DOCUMENT_INTELLIGENCE_ENGINE_C_API_H
#define DOCUMENT_INTELLIGENCE_ENGINE_C_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(DIE_C_API_BUILD)
#define DIE_C_API __declspec(dllexport)
#else
#define DIE_C_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define DIE_C_API __attribute__((visibility("default")))
#else
#define DIE_C_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DIE_ABI_VERSION 1u

typedef struct die_engine die_engine_t;
typedef struct die_document die_document_t;
typedef struct die_error die_error_t;

typedef int32_t die_result_t;
enum {
    DIE_RESULT_OK = 0,
    DIE_RESULT_INVALID_ARGUMENT = 1,
    DIE_RESULT_CONFIGURATION_ERROR = 2,
    DIE_RESULT_PARSE_ERROR = 3,
    DIE_RESULT_SERIALIZATION_ERROR = 4,
    DIE_RESULT_INTERNAL_ERROR = 5
};

typedef int32_t die_engine_state_t;
enum {
    DIE_ENGINE_STATE_INVALID = 0,
    DIE_ENGINE_STATE_READY = 1,
    DIE_ENGINE_STATE_PARSING = 2,
    DIE_ENGINE_STATE_INITIALIZATION_FAILED = 3,
    DIE_ENGINE_STATE_MOVED_FROM = 4
};

DIE_C_API uint32_t die_abi_version(void);
DIE_C_API const char* die_engine_version(void);

DIE_C_API die_result_t die_engine_create(const char* config_json, die_engine_t** out_engine, die_error_t** out_error);

DIE_C_API die_engine_state_t die_engine_get_state(const die_engine_t* engine);

DIE_C_API die_result_t die_engine_parse(die_engine_t* engine,
                                        const char* options_json,
                                        die_document_t** out_document,
                                        die_error_t** out_error);

DIE_C_API const char* die_document_json(const die_document_t* document);
DIE_C_API size_t die_document_json_size(const die_document_t* document);

DIE_C_API die_result_t die_error_result(const die_error_t* error);
DIE_C_API const char* die_error_code(const die_error_t* error);
DIE_C_API const char* die_error_message(const die_error_t* error);
DIE_C_API const char* die_error_stage(const die_error_t* error);
DIE_C_API int die_error_retryable(const die_error_t* error);

DIE_C_API void die_document_destroy(die_document_t* document);
DIE_C_API void die_engine_destroy(die_engine_t* engine);
DIE_C_API void die_error_destroy(die_error_t* error);

#ifdef __cplusplus
}
#endif

#endif
