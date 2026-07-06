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

template <typename T> class Result {
public:
    Result(T value) : status_(Status::ok()), value_(std::move(value)), has_value_(true) {}
    Result(Status status) : status_(std::move(status)) {}

    bool ok() const { return status_.okStatus(); }
    const Status& status() const { return status_; }
    const T& value() const { return value_; }
    T& value() { return value_; }

private:
    Status status_;
    T value_{};
    bool has_value_ = false;
};

} // namespace doc_parser::common
