/**
 * @file RunIdentity.h
 * @brief Linux run-instance identity, independent of filenames or scientific state.
 * Workflow: obtain OS entropy once, format UUID v4, retain it in the output owner.
 * Fail explicitly if entropy cannot be obtained; never replace it with a PID/time.
 */
#pragma once
#include <array>
#include <cerrno>
#include <stdexcept>
#include <string>
#ifdef __linux__
#include <sys/random.h>
#endif
namespace arch::core {
inline bool valid_run_identity(const std::string& value) {
    if (value.size()!=36 || value[14]!='4' ||
        std::string("89ab").find(value[19])==std::string::npos) return false;
    for (std::size_t i=0; i<value.size(); ++i) {
        if (i==8 || i==13 || i==18 || i==23) {
            if (value[i]!='-') return false;
        } else if (!((value[i]>='0' && value[i]<='9') ||
                     (value[i]>='a' && value[i]<='f'))) return false;
    }
    return true;
}
inline std::string new_run_identity() {
#ifdef __linux__
    std::array<unsigned char,16> bytes{};
    std::size_t filled = 0;
    while (filled < bytes.size()) {
        const auto count = ::getrandom(bytes.data()+filled, bytes.size()-filled, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("Cannot obtain run identity entropy");
        filled += static_cast<std::size_t>(count);
    }
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
#else
    throw std::runtime_error("Run identity requires a supported OS entropy provider");
#endif
}
} // namespace arch::core
