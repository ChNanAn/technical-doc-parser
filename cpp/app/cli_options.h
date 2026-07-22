#pragma once

#include <string>

namespace doc_parser::app {

struct CliOptions {
    std::string input_pdf;
    std::string output_dir = "output";
    int dpi = 200;
    bool debug = false;
    std::string document_backend = "auto";
    std::string ocr_backend = "auto";
    std::string layout_backend = "auto";
    std::string table_backend = "auto";
    std::string backend_config;
};

} // namespace doc_parser::app
