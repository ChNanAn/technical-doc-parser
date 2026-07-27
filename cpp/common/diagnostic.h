#pragma once

#include <map>
#include <string>

namespace doc_parser::common {

struct Diagnostic {
    std::string code;
    std::string message;
    std::string stage;
    int page_number = 0;
    std::map<std::string, std::string> details;
};

} // namespace doc_parser::common
