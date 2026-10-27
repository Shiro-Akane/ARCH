/**
 * @file RunIdentity.h
 * @brief OS run-instance identity, independent of filenames or scientific state.
 * Workflow: obtain OS entropy once, format UUID v4, retain it in the output owner.
 * Fail explicitly if entropy cannot be obtained; never replace it with a PID/time.
 */
#pragma once
#include <array>
#include <cerrno>
#include <stdexcept>
#include <string>
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
std::string new_run_identity();
} // namespace arch::core
