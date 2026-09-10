#pragma once

#include <filesystem>

namespace doc_parser::platform {

// Cooperates with API downloads and retention. The lock inode is never removed.
class RunArtifactLock {
public:
    explicit RunArtifactLock(const std::filesystem::path& directory);
    ~RunArtifactLock();
    RunArtifactLock(const RunArtifactLock&) = delete;
    RunArtifactLock& operator=(const RunArtifactLock&) = delete;

private:
    int descriptor_ = -1;
};

} // namespace doc_parser::platform
