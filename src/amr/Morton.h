/**
 * @file Morton.h
 * @brief 64-bit Morton encoding (Z-curve) for 3D AMR blocks.
 *
 * Host topology keys use the high four bits for the refinement level and the
 * low sixty bits for interleaved logical coordinates: x, y, z occupy bit
 * positions 3n, 3n+1, 3n+2. Coordinates are nonnegative block indices, not
 * physical distances. Callers must validate the representable domain before
 * encoding, because the bit operations mask rather than reject overflow.
 */

#pragma once

#include <cstdint>

namespace amr {

inline constexpr int kMortonLevelBits = 4;
inline constexpr int kMaxRefinementLevel = (1 << kMortonLevelBits) - 1;
inline constexpr uint32_t kMortonCoordinateMask = (1u << 20) - 1u;

/**
 * @brief Spreads one 20-bit coordinate into every third bit of a 60-bit word.
 */
inline uint64_t splitBy3(uint64_t a) {
    a &= kMortonCoordinateMask;
    a = (a | (a << 32)) & 0x1f00000000ffff;
    a = (a | (a << 16)) & 0x1f0000ff0000ff;
    a = (a | (a << 8))  & 0x100f00f00f00f00f;
    a = (a | (a << 4))  & 0x10c30c30c30c30c3;
    a = (a | (a << 2))  & 0x1249249249249249;
    return a;
}

/**
 * @brief Extracts every third bit from the low 60 Morton-coordinate bits.
 */
inline uint64_t getThirdBits(uint64_t a) {
    a &= 0x1249249249249249;
    a = (a ^ (a >> 2))  & 0x10c30c30c30c30c3;
    a = (a ^ (a >> 4))  & 0x100f00f00f00f00f;
    a = (a ^ (a >> 8))  & 0x1f0000ff0000ff;
    a = (a ^ (a >> 16)) & 0x1f00000000ffff;
    a = (a ^ (a >> 32)) & kMortonCoordinateMask;
    return a;
}

/**
 * @brief Computes a Morton code with four level bits and 20 bits per coordinate.
 */
inline uint64_t encodeMorton(int level, uint32_t x, uint32_t y, uint32_t z) {
    uint64_t code = 0;
    code |= (static_cast<uint64_t>(level) & kMaxRefinementLevel) << 60;
    code |= (splitBy3(z) << 2) | (splitBy3(y) << 1) | splitBy3(x);
    return code;
}

/**
 * @brief Decodes a Morton code into its level and logical coordinates.
 */
inline void decodeMorton(uint64_t code, int& level, uint32_t& x, uint32_t& y, uint32_t& z) {
    level = static_cast<int>((code >> 60) & kMaxRefinementLevel);
    x = static_cast<uint32_t>(getThirdBits(code));
    y = static_cast<uint32_t>(getThirdBits(code >> 1));
    z = static_cast<uint32_t>(getThirdBits(code >> 2));
}

} // namespace amr
