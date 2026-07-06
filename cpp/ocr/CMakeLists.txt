add_library(doc_parser_ocr
    ocr_service.cpp
    tesseract_cli_ocr_backend.cpp
)

target_include_directories(doc_parser_ocr
    PUBLIC
        ${PROJECT_SOURCE_DIR}/cpp
)

target_link_libraries(doc_parser_ocr
    PUBLIC
        doc_parser_document
)

doc_parser_set_common_warnings(doc_parser_ocr)
