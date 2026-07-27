#pragma once

#include "common/status.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace doc_parser::common {

struct FileFingerprint {
    std::uintmax_t size_bytes = 0;
    std::string sha256;
};

Status fingerprintFile(const std::filesystem::path& path, FileFingerprint& fingerprint);

} // namespace doc_parser::common
