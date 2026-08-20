// Nuclear constants translated from the const.dek files distributed with
// Frank Timmes's public networks: https://cococubed.com/code_pages/burn.shtml.
#pragma once

namespace timmes::constants {

// Timmes const.dek values used by the four translated networks.
inline constexpr double avo = 6.0221417930e23;
inline constexpr double clight = 2.99792458e10;
inline constexpr double kerg = 1.380650424e-16;
inline constexpr double ev2erg = 1.60217648740e-12;
inline constexpr double amu = 1.66053878283e-24;
inline constexpr double mn = 1.67492721184e-24;
inline constexpr double mp = 1.67262163783e-24;
inline constexpr double mev2erg = ev2erg * 1.0e6;
inline constexpr double mev2gr = mev2erg / (clight * clight);
inline constexpr double enuc_conv = mev2erg * avo;
inline constexpr double enuc_conv2 = -avo * clight * clight;

} // namespace timmes::constants
