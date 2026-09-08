/**
 * @file TabularBaryonSource.h
 * @brief Host-only decoding of finite-temperature, 16-column baryon tables.
 *
 * The EOS2/EOS4 author products share one file schema. This adapter preserves
 * their samples and energy references; component completion, interpolation,
 * positivity gauges and device storage belong to the consuming EOS owner.
 */
#pragma once

#include <string>
#include <vector>

namespace tabular_eos::source {

// One source-format contract shared by decoding and interpretation fingerprints.
// These are the author table's conventions, not current fundamental constants.
namespace baryon_ascii16 {
inline constexpr double baryon_mass_g = 1.66054e-24;
inline constexpr double energy_reference_mev = 931.494;
inline constexpr double free_energy_reference_mev = 938.0;
inline constexpr double free_energy_alignment_mev =
    free_energy_reference_mev - energy_reference_mev;
inline constexpr char interpretation[] =
    "baryon-ascii16-v1;axes=native-logrho-logT-Ye;"
    "rho=cgs;T=MeV;mu=1.66054e-24g;Eref=931.494MeV;Fref=938MeV;"
    "Faligned=Eref;components=baryons;coordinates=printed-precision-mask";
} // namespace baryon_ascii16

struct BaryonTable {
    // Native coordinate nodes. Temperature is kelvin; density is g/cm^3.
    std::vector<double> log_density, log_temperature;
    std::vector<double> density, temperature, electron_fraction;

    // Dense C-order [rho,T,Ye]. Specific energies are erg/g, entropy is
    // erg/(g K), and pressure is dyn/cm^2. Free energy has been aligned to
    // the energy reference; no positivity shift has been applied.
    std::vector<double> pressure, energy, free_energy, entropy;

    // Source quantities remain available for independent unit/reference and
    // coordinate checks, including the printed per-node rather than nominal Ye.
    std::vector<double> source_number_density_fm3, source_electron_fraction;
    std::vector<double> source_pressure_mev_fm3, source_energy_mev;
    std::vector<double> source_free_energy_mev, source_entropy_kb;
    // A source-coordinate/number-density mismatch is a hole, not a moved node.
    // Consumers must also propagate this mask through derivative stencils.
    std::vector<double> source_valid;

    // Source-format constants, not new fundamental constants or fitted
    // parameters. The fixed rounded cgs mass is checked against printed nB.
    double baryon_mass_g = baryon_ascii16::baryon_mass_g;
    double energy_reference_mev = baryon_ascii16::energy_reference_mev;
    double free_energy_reference_mev = baryon_ascii16::free_energy_reference_mev;
    double free_energy_alignment_mev = baryon_ascii16::free_energy_alignment_mev;
    std::string interpretation = baryon_ascii16::interpretation;
};

// Inspect content, not an extension, filename or EOS model name. This probe
// recognizes the header; the complete reader owns all structural validation.
bool is_baryon_ascii_table(const std::string& path);
BaryonTable read_baryon_ascii_table(const std::string& path);

} // namespace tabular_eos::source
