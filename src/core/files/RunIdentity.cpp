/**
 * @file RunIdentity.cpp
 * @brief Fill one UUIDv4 from the OS entropy source without exposing OS headers.
 * The shared formatter and fail-closed behavior are identical on supported hosts.
 */
#include "core/files/RunIdentity.h"

#ifdef __linux__
#include <sys/random.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace arch::core {
std::string new_run_identity() {
    std::array<unsigned char,16> bytes{};
#ifdef __linux__
    std::size_t filled = 0;
    while (filled < bytes.size()) {
        const auto count = ::getrandom(bytes.data()+filled, bytes.size()-filled, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("Cannot obtain run identity entropy");
        filled += static_cast<std::size_t>(count);
    }
#elif defined(_WIN32)
    if (::BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("Cannot obtain run identity entropy");
#else
    throw std::runtime_error("Run identity requires a supported OS entropy provider");
#endif
    bytes[6] = (bytes[6]&0x0f)|0x40;
    bytes[8] = (bytes[8]&0x3f)|0x80;
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (std::size_t i=0; i<bytes.size(); ++i) {
        if (i==4 || i==6 || i==8 || i==10) result += '-';
        result += digits[bytes[i]>>4];
        result += digits[bytes[i]&15];
    }
    return result;
}
} // namespace arch::core
