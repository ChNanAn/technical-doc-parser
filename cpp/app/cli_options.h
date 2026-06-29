#pragma once

#include <string>

namespace doc_parser::app {

struct CliOptions {
    std::string input_pdf;
    std::string output_dir = "output";
    int dpi = 200;
    bool debug = false;
};

}  // namespace doc_parser::app
