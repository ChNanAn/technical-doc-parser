#include <document_intelligence_engine/c_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int report_error(const char* operation, die_result_t result, die_error_t* error) {
    fprintf(stderr,
            "%s failed: result=%d stage=%s code=%s message=%s retryable=%d\n",
            operation,
            (int)result,
            die_error_stage(error),
            die_error_code(error),
            die_error_message(error),
            die_error_retryable(error));
    die_error_destroy(error);
    return 1;
}

static int expect_error(const char* operation,
                        die_result_t actual_result,
                        die_result_t expected_result,
                        const char* expected_code,
                        const char* expected_stage,
                        die_error_t* error) {
    int failed = actual_result != expected_result || error == NULL || die_error_result(error) != expected_result ||
                 strcmp(die_error_code(error), expected_code) != 0 ||
                 strcmp(die_error_stage(error), expected_stage) != 0 || strlen(die_error_message(error)) == 0;
    if (failed) {
        fprintf(stderr,
                "%s returned an unexpected error: result=%d error_result=%d "
                "stage=%s code=%s message=%s\n",
                operation,
                (int)actual_result,
                (int)die_error_result(error),
                die_error_stage(error),
                die_error_code(error),
                die_error_message(error));
    }
    die_error_destroy(error);
    return failed;
}

int main(int argc, char** argv) {
    static const char* valid_config = "{\"schema_version\":1,\"backends\":{\"document\":\"pdf\",\"ocr\":\"noop\","
                                      "\"layout\":\"text\",\"table\":\"text\"}}";
    static const char* unavailable_config = "{\"schema_version\":1,\"backends\":{\"document\":\"not-registered\","
                                            "\"ocr\":\"noop\",\"layout\":\"text\",\"table\":\"text\"}}";
    static const char* missing_model_config = "{\"schema_version\":1,\"backends\":{\"document\":\"pdf\","
                                              "\"ocr\":\"paddle\",\"layout\":\"text\",\"table\":\"text\"}}";
    char options[8192];
    die_engine_t* engine = NULL;
    die_document_t* document = NULL;
    die_error_t* error = NULL;
    die_result_t result;

    if (argc != 3) {
        fprintf(stderr, "usage: c_api_smoke INPUT_PDF OUTPUT_DIRECTORY\n");
        return 2;
    }
    if (sizeof(die_result_t) != sizeof(int32_t) || sizeof(die_engine_state_t) != sizeof(int32_t) ||
        die_abi_version() != DIE_ABI_VERSION || strlen(die_engine_version()) == 0) {
        fprintf(stderr, "C ABI version query failed\n");
        return 1;
    }

    result = die_engine_create("{", &engine, &error);
    if (engine != NULL || expect_error("malformed engine configuration",
                                       result,
                                       DIE_RESULT_INVALID_ARGUMENT,
                                       "c_api.invalid_config",
                                       "configure",
                                       error)) {
        return 1;
    }
    error = NULL;

    result = die_engine_create("{\"schema_version\":1,\"unknown\":true}", &engine, &error);
    if (engine != NULL || expect_error("unknown engine configuration field",
                                       result,
                                       DIE_RESULT_INVALID_ARGUMENT,
                                       "c_api.invalid_config",
                                       "configure",
                                       error)) {
        return 1;
    }
    error = NULL;

    result =
        die_engine_create("{\"schema_version\":1,\"backends\":{\"document\":\"pdf\\u0000ignored\"}}", &engine, &error);
    if (engine != NULL || expect_error("NUL in engine configuration",
                                       result,
                                       DIE_RESULT_INVALID_ARGUMENT,
                                       "c_api.invalid_config",
                                       "configure",
                                       error)) {
        return 1;
    }
    error = NULL;

    result = die_engine_create(unavailable_config, &engine, &error);
    if (engine != NULL || expect_error("unavailable engine backend",
                                       result,
                                       DIE_RESULT_CONFIGURATION_ERROR,
                                       "configure.backend_unknown",
                                       "configure",
                                       error)) {
        return 1;
    }
    error = NULL;

    result = die_engine_create(missing_model_config, &engine, &error);
    if (engine != NULL || expect_error("missing explicit C ABI model paths",
                                       result,
                                       DIE_RESULT_CONFIGURATION_ERROR,
                                       "configure.backend_unavailable",
                                       "configure",
                                       error)) {
        return 1;
    }
    error = NULL;

    result = die_engine_create(valid_config, &engine, &error);
    if (result != DIE_RESULT_OK) {
        return report_error("die_engine_create", result, error);
    }
    if (engine == NULL || error != NULL || die_engine_get_state(engine) != DIE_ENGINE_STATE_READY) {
        fprintf(stderr, "engine create returned an invalid handle or state\n");
        die_engine_destroy(engine);
        return 1;
    }

    result = die_engine_parse(engine, "{", &document, &error);
    if (document != NULL ||
        expect_error(
            "malformed parse options", result, DIE_RESULT_INVALID_ARGUMENT, "c_api.invalid_options", "parse", error)) {
        die_engine_destroy(engine);
        return 1;
    }
    error = NULL;

    result = die_engine_parse(engine,
                              "{\"schema_version\":1,\"input_path\":\"input.pdf\","
                              "\"output_directory\":\"output\",\"unknown\":true}",
                              &document,
                              &error);
    if (document != NULL || expect_error("unknown parse option field",
                                         result,
                                         DIE_RESULT_INVALID_ARGUMENT,
                                         "c_api.invalid_options",
                                         "parse",
                                         error)) {
        die_engine_destroy(engine);
        return 1;
    }
    error = NULL;

    if (snprintf(options,
                 sizeof(options),
                 "{\"schema_version\":1,\"input_path\":\"%s.missing\","
                 "\"output_directory\":\"%s\"}",
                 argv[1],
                 argv[2]) >= (int)sizeof(options)) {
        fprintf(stderr, "test paths exceed options buffer\n");
        die_engine_destroy(engine);
        return 1;
    }
    result = die_engine_parse(engine, options, &document, &error);
    if (document != NULL || result != DIE_RESULT_PARSE_ERROR || error == NULL ||
        die_error_result(error) != DIE_RESULT_PARSE_ERROR || strlen(die_error_code(error)) == 0 ||
        strlen(die_error_message(error)) == 0 || strlen(die_error_stage(error)) == 0) {
        fprintf(stderr, "missing input did not return a structured parse error\n");
        die_error_destroy(error);
        die_engine_destroy(engine);
        return 1;
    }
    die_error_destroy(error);
    error = NULL;

    if (snprintf(options,
                 sizeof(options),
                 "{\"schema_version\":1,\"input_path\":\"%s\","
                 "\"output_directory\":\"%s\",\"dpi\":72,\"run_id\":\"c_api_smoke\"}",
                 argv[1],
                 argv[2]) >= (int)sizeof(options)) {
        fprintf(stderr, "test paths exceed options buffer\n");
        die_engine_destroy(engine);
        return 1;
    }
    result = die_engine_parse(engine, options, &document, &error);
    if (result != DIE_RESULT_OK) {
        die_engine_destroy(engine);
        return report_error("die_engine_parse", result, error);
    }
    if (document == NULL || error != NULL || die_document_json_size(document) != strlen(die_document_json(document)) ||
        strstr(die_document_json(document), "\"schema_version\": 1") == NULL ||
        strstr(die_document_json(document), "\"run_id\": \"c_api_smoke\"") == NULL) {
        fprintf(stderr, "C API returned an invalid Document v1 JSON result\n");
        die_document_destroy(document);
        die_engine_destroy(engine);
        return 1;
    }

    die_document_destroy(document);
    die_engine_destroy(engine);
    die_document_destroy(NULL);
    die_engine_destroy(NULL);
    die_error_destroy(NULL);
    return 0;
}
