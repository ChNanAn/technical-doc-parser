#include "common/atomic_output_file.h"

#include <atomic>
#include <chrono>
#include <system_error>
#include <utility>

namespace doc_parser::common {

AtomicOutputFile::AtomicOutputFile(std::filesystem::path destination) : destination_(std::move(destination)) {
    std::error_code error;
    const auto type = std::filesystem::symlink_status(destination_, error).type();
    if ((error && error != std::errc::no_such_file_or_directory) ||
        (type != std::filesystem::file_type::not_found && type != std::filesystem::file_type::regular)) {
        return;
    }
    static std::atomic<unsigned long long> sequence{0};
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto candidate = destination_.parent_path() /
                               (".die-output-" + std::to_string(tick) + "-" + std::to_string(sequence.fetch_add(1)));
        error.clear();
        if (std::filesystem::create_directory(candidate, error)) {
            temporary_directory_ = candidate;
            temporary_file_ = candidate / "content";
            output_.open(temporary_file_, std::ios::binary | std::ios::trunc);
            return;
        }
        if (error && error != std::errc::file_exists) {
            return;
        }
    }
}

AtomicOutputFile::~AtomicOutputFile() {
    if (output_.is_open()) {
        output_.close();
    }
    std::error_code ignored;
    if (!temporary_file_.empty()) {
        std::filesystem::remove(temporary_file_, ignored);
    }
    if (!temporary_directory_.empty()) {
        std::filesystem::remove(temporary_directory_, ignored);
    }
}

bool AtomicOutputFile::commit() {
    if (!output_.is_open()) {
        return false;
    }
    output_.flush();
    const bool written = static_cast<bool>(output_);
    output_.close();
    if (!written || !output_) {
        return false;
    }
    std::error_code error;
    std::filesystem::rename(temporary_file_, destination_, error);
    return !error;
}

} // namespace doc_parser::common
