#include "run_artifact_lock.h"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace doc_parser::platform {

RunArtifactLock::RunArtifactLock(const std::filesystem::path& directory) {
    const int root = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0) {
        throw std::system_error(errno, std::generic_category(), "open Run artifact directory");
    }
    try {
        descriptor_ = ::openat(root, ".artifacts.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (descriptor_ < 0) {
            throw std::system_error(errno, std::generic_category(), "open Run artifact lock");
        }
        struct stat info {};
        if (::fstat(descriptor_, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1) {
            throw std::runtime_error("Run artifact lock must be a single-link regular file");
        }
        if (::flock(descriptor_, LOCK_SH | LOCK_NB) != 0) {
            throw std::system_error(errno, std::generic_category(), "Run artifact directory is in use by cleanup");
        }
        if (::fstatat(root, ".artifacts-expired", &info, AT_SYMLINK_NOFOLLOW) == 0) {
            throw std::runtime_error("Run artifacts have expired");
        }
        if (errno != ENOENT) {
            throw std::system_error(errno, std::generic_category(), "check Run artifact expiry");
        }
    } catch (...) {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
            descriptor_ = -1;
        }
        ::close(root);
        throw;
    }
    ::close(root);
}

RunArtifactLock::~RunArtifactLock() {
    if (descriptor_ >= 0) {
        ::close(descriptor_);
    }
}

} // namespace doc_parser::platform
