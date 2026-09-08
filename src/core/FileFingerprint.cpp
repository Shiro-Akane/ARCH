/**
 * @file FileFingerprint.cpp
 * @brief Dependency-free streaming SHA-256 file fingerprints.
 */

#include "FileFingerprint.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace arch::core {
namespace {

constexpr std::array<std::uint32_t, 64> sha256_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

class Sha256 {
public:
    void update(const std::uint8_t* bytes, std::size_t count)
    {
        if (count > (std::numeric_limits<std::uint64_t>::max() - bit_count_) / 8U)
            throw std::overflow_error("SHA-256 input is too large");
        bit_count_ += static_cast<std::uint64_t>(count) * 8U;
        while (count != 0) {
            const std::size_t copied = std::min(count, block_.size() - used_);
            std::copy_n(bytes, copied, block_.begin() + used_);
            used_ += copied;
            bytes += copied;
            count -= copied;
            if (used_ == block_.size()) {
                transform(block_.data());
                used_ = 0;
            }
        }
    }

    std::string finish()
    {
        block_[used_++] = 0x80U;
        if (used_ > 56) {
            std::fill(block_.begin() + used_, block_.end(), 0U);
            transform(block_.data());
            used_ = 0;
        }
        std::fill(block_.begin() + used_, block_.begin() + 56, 0U);
        for (int byte = 0; byte < 8; ++byte) {
            block_[56 + byte] = static_cast<std::uint8_t>(
                bit_count_ >> (56 - 8 * byte));
        }
        transform(block_.data());

        std::ostringstream digest;
        digest << std::hex << std::setfill('0');
        for (const std::uint32_t word : state_)
            digest << std::setw(8) << word;
        return digest.str();
    }

private:
    static constexpr std::uint32_t rotate_right(
        std::uint32_t value, unsigned int shift) noexcept
    {
        return std::rotr(value, static_cast<int>(shift));
    }

    void transform(const std::uint8_t* block)
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            words[i] = (static_cast<std::uint32_t>(block[4 * i]) << 24)
                     | (static_cast<std::uint32_t>(block[4 * i + 1]) << 16)
                     | (static_cast<std::uint32_t>(block[4 * i + 2]) << 8)
                     | static_cast<std::uint32_t>(block[4 * i + 3]);
        }
        for (std::size_t i = 16; i < words.size(); ++i) {
            const std::uint32_t s0 = rotate_right(words[i - 15], 7)
                ^ rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const std::uint32_t s1 = rotate_right(words[i - 2], 17)
                ^ rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (std::size_t i = 0; i < words.size(); ++i) {
            const std::uint32_t sum1 = rotate_right(e, 6)
                ^ rotate_right(e, 11) ^ rotate_right(e, 25);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + sum1 + choice
                + sha256_constants[i] + words[i];
            const std::uint32_t sum0 = rotate_right(a, 2)
                ^ rotate_right(a, 13) ^ rotate_right(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
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

    std::array<std::uint32_t, 8> state_{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::uint8_t, 64> block_{};
    std::size_t used_ = 0;
    std::uint64_t bit_count_ = 0;
};

} // namespace

std::string string_sha256(std::string_view bytes)
{
    Sha256 hash;
    hash.update(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    return hash.finish();
}

std::string file_sha256(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error(
            "Cannot open file for SHA-256 fingerprint: " + path);

    Sha256 hash;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            hash.update(reinterpret_cast<const std::uint8_t*>(buffer.data()),
                        static_cast<std::size_t>(count));
        }
    }
    if (!input.eof())
        throw std::runtime_error(
            "Failed reading file for SHA-256 fingerprint: " + path);
    return hash.finish();
}

} // namespace arch::core
