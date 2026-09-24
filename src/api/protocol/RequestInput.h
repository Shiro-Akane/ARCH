/**
 * @file RequestInput.h
 * @brief Validate bounded request text before parsing and dispatch.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Validate bounded request text before parsing and dispatch.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include <string_view>

namespace arch::api::detail {
/** Reject malformed, overlong, surrogate and embedded-NUL UTF-8 input. */
inline bool ValidUtf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first == 0) return false;
        if (first < 0x80) continue;
        int more = 0;
        unsigned value = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { more = 1; value = first & 31; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { more = 2; value = first & 15; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { more = 3; value = first & 7; minimum = 0x10000; }
        else return false;
        while (more--) {
            if (i == text.size()) return false;
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
}
