#pragma once

#include <string>
#include <utility>

namespace doc_parser::common {

class Status {
public:
    static Status ok() { return {}; }

    static Status error(std::string code, std::string message) {
        Status status;
        status.ok_ = false;
        status.code_ = std::move(code);
        status.message_ = std::move(message);
        return status;
    }

    bool okStatus() const { return ok_; }
    const std::string& code() const { return code_; }
    const std::string& message() const { return message_; }

private:
    bool ok_ = true;
    std::string code_;
    std::string message_;
};

} // namespace doc_parser::common
