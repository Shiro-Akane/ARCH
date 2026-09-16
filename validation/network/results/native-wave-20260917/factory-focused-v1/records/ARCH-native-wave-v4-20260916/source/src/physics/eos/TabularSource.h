/**
 * @file TabularSource.h
 * @brief Host inspection of supported EOS file formats and their physical identity.
 *
 * Format families select data interpretation, not another runtime EOS type.
 * Nuclear-equilibrium tables already contain the nuclear binding contribution;
 * callers must not attach an independent kinetic nuclear-energy source.
 */
#pragma once

#include <string>

enum class TabularSourceFormat { Normalized, EosDriver, BaryonAscii };

// Component declarations describe physics, never a model-name switch. Legacy
// normalized tables and EOSDriver products already represent a complete EOS.
struct TabularComponents {
    bool declared = false;
    bool electrons_positrons = true;
    bool photons = true;
    bool needs_completion() const { return !electrons_positrons || !photons; }
};

inline constexpr const char* default_tabular_helm_path =
    "EOS_toolkit/tables/helmholtz/helm_table.dat";

struct TabularSourceInfo {
    int rank = 0;
    TabularSourceFormat format = TabularSourceFormat::Normalized;
    bool nuclear_equilibrium = false;
    std::string interpretation;
    TabularComponents components{};
    double baryon_mass_g = 0.0; // Zero retains the component provider's native mass convention.
};

TabularSourceInfo inspect_tabular_source(const std::string& path);

// Normalized inputs retain their existing file identity. Native inputs bind
// both the source bytes and the complete maintained interpretation contract.
std::string tabular_source_fingerprint(const std::string& path,
    const std::string& helm_path = default_tabular_helm_path);
