/**
 * CGS units for fields whose production definition has a fixed unit.
 * ENTR is excluded: its exponent comes from the local EOS Gamma1.
 * These labels never rescale the underlying field values.
 */
#pragma once
#include <string_view>
namespace arch::fields {
inline std::string_view cgs_unit(std::string_view key) {
    if (key == "DENS") return "g/cm^3";
    if (key == "TEMP") return "K";
    if (key == "PRES" || key == "ENER") return "erg/cm^3";
    if (key == "GPOT") return "cm^2/s^2";
    if (key == "GACX" || key == "GACY" || key == "GACZ") return "cm/s^2";
    if (key == "EINT") return "erg/g";
    if (key == "VELX" || key == "VELY" || key == "VELZ") return "cm/s";
    return {};
}
}
