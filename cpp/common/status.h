#pragma once

#include <string>
#include <utility>

namespace doc_parser::common {

class Status {
public:
    static Status ok() { return {}; }

    static Status error(std::string code, std::string message, std::string stage = {}, bool retryable = false) {
        Status status;
        status.ok_ = false;
        status.code_ = std::move(code);
        status.message_ = std::move(message);
        status.stage_ = std::move(stage);
        status.retryable_ = retryable;
        return status;
    }

    bool okStatus() const { return ok_; }
    const std::string& code() const { return code_; }
    const std::string& message() const { return message_; }
    const std::string& stage() const { return stage_; }
    bool retryable() const { return retryable_; }

private:
    bool ok_ = true;
    std::string code_;
    std::string message_;
    std::string stage_;
    bool retryable_ = false;
};

} // namespace doc_parser::common
