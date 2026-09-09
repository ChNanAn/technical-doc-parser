#pragma once

#include <filesystem>
#include <fstream>

namespace doc_parser::common {

// A reader sees the previous file until commit() publishes the complete replacement.
// Each writer owns an exclusive temporary directory on the destination filesystem.
class AtomicOutputFile {
public:
    explicit AtomicOutputFile(std::filesystem::path destination);
    ~AtomicOutputFile();
    AtomicOutputFile(const AtomicOutputFile&) = delete;
    AtomicOutputFile& operator=(const AtomicOutputFile&) = delete;

    bool isOpen() const { return output_.is_open(); }
    std::ostream& stream() { return output_; }
    bool commit();

private:
    std::filesystem::path destination_;
    std::filesystem::path temporary_directory_;
    std::filesystem::path temporary_file_;
    std::ofstream output_;
};

} // namespace doc_parser::common
