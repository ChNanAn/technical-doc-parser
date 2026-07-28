#include "common/file_fingerprint.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace doc_parser::common {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
}};

std::uint32_t rotateRight(std::uint32_t value, unsigned int bits) { return (value >> bits) | (value << (32U - bits)); }

class Sha256 {
public:
    void update(const std::uint8_t* data, std::size_t size) {
        total_size_ += static_cast<std::uint64_t>(size);
        while (size > 0) {
            const std::size_t copied = std::min(size, block_.size() - block_size_);
            std::copy_n(data, copied, block_.begin() + static_cast<std::ptrdiff_t>(block_size_));
            block_size_ += copied;
            data += copied;
            size -= copied;
            if (block_size_ == block_.size()) {
                transform(block_);
                block_size_ = 0;
            }
        }
    }

    std::string finish() {
        const std::uint64_t total_bits = total_size_ * 8U;
        block_[block_size_++] = 0x80U;
        if (block_size_ > 56U) {
            std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.end(), 0U);
            transform(block_);
            block_size_ = 0;
        }
        std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.begin() + 56, 0U);
        for (std::size_t index = 0; index < 8; ++index) {
            block_[63U - index] = static_cast<std::uint8_t>(total_bits >> (index * 8U));
        }
        transform(block_);

        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (const std::uint32_t value : state_) {
            output << std::setw(8) << value;
        }
        return output.str();
    }

private:
    void transform(const std::array<std::uint8_t, 64>& block) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const std::size_t offset = index * 4;
            words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                           (static_cast<std::uint32_t>(block[offset + 1]) << 16U) |
                           (static_cast<std::uint32_t>(block[offset + 2]) << 8U) |
                           static_cast<std::uint32_t>(block[offset + 3]);
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const std::uint32_t s0 = rotateRight(words[index - 15], 7) ^ rotateRight(words[index - 15], 18) ^
                                     (words[index - 15] >> 3U);
            const std::uint32_t s1 = rotateRight(words[index - 2], 17) ^ rotateRight(words[index - 2], 19) ^
                                     (words[index - 2] >> 10U);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const std::uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t first = h + sum1 + choose + kRoundConstants[index] + words[index];
            const std::uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t second = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{{
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    }};
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_ = 0;
    std::uint64_t total_size_ = 0;
};

} // namespace

Status fingerprintFile(const std::filesystem::path& path, FileFingerprint& fingerprint) {
    fingerprint = {};
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Status::error("source.fingerprint_open_failed",
                             "failed to open source for fingerprinting: " + path.string());
    }

    Sha256 digest;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            digest.update(reinterpret_cast<const std::uint8_t*>(buffer.data()), static_cast<std::size_t>(count));
            fingerprint.size_bytes += static_cast<std::uintmax_t>(count);
        }
    }
    if (!input.eof()) {
        fingerprint = {};
        return Status::error("source.fingerprint_read_failed",
                             "failed while reading source for fingerprinting: " + path.string());
    }
    fingerprint.sha256 = digest.finish();
    return Status::ok();
}

} // namespace doc_parser::common
