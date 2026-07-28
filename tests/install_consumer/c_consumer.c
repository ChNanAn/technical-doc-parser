#include <document_intelligence_engine/c_api.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    static const char* config = "{\"schema_version\":1,\"backends\":{\"document\":\"pdf\",\"ocr\":\"noop\","
                                "\"layout\":\"text\",\"table\":\"text\"}}";
    char options[8192];
    die_engine_t* engine = NULL;
    die_document_t* document = NULL;
    die_error_t* error = NULL;

    if (argc != 3) {
        return 2;
    }
    if (die_engine_create(config, &engine, &error) != DIE_RESULT_OK) {
        fprintf(stderr, "%s: %s\n", die_error_code(error), die_error_message(error));
        die_error_destroy(error);
        return 1;
    }
    if (snprintf(options,
                 sizeof(options),
                 "{\"schema_version\":1,\"input_path\":\"%s\","
                 "\"output_directory\":\"%s\",\"dpi\":72}",
                 argv[1],
                 argv[2]) >= (int)sizeof(options)) {
        die_engine_destroy(engine);
        return 1;
    }
    if (die_engine_parse(engine, options, &document, &error) != DIE_RESULT_OK) {
        fprintf(stderr, "%s: %s\n", die_error_code(error), die_error_message(error));
        die_error_destroy(error);
        die_engine_destroy(engine);
        return 1;
    }
    if (strstr(die_document_json(document), "\"schema_version\": 1") == NULL) {
        die_document_destroy(document);
        die_engine_destroy(engine);
        return 1;
    }
    die_document_destroy(document);
    die_engine_destroy(engine);
    return 0;
}
