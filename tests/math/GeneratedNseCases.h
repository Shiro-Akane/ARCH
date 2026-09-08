/** Shared host/device witnesses for generated-data NSE and its burn handoff. */
#pragma once

#include "numerics/burnsolver/odeFunction.h"
#include "../fixtures/GeneratedNseReference.h"

namespace arch::test::generated_nse {

struct NuclearData {
    static constexpr int NSE_DATA_VERSION = 1;
    static constexpr bool SUPPORTS_NSE = true;
    static constexpr double NSE_T_MIN = 1.0e8, NSE_T_MAX = 1.0e10;
    static constexpr double NSE_ATOMIC_MASS_UNIT = 1.66053906660e-24;
    static constexpr double NSE_K_BOLTZMANN = 1.380649e-16;
    static constexpr double NSE_K_BOLTZMANN_MEV = 8.617333262145e-11;
    static constexpr double NSE_HBAR = 1.054571817e-27;
    static constexpr double ENERGY_CONVERSION = -(6.02214076e23 * 8.9875517873681764e20);
    ARCH_HOST_DEVICE static double nse_log_partition(int, double) { return 0.0; }
};

// Three physical species allow an independent quadratic equilibrium solution.
// The reaction-energy masses deliberately retain more information than the
// rounded binding datum, so an independent binding-energy return path fails.
struct LightNetwork : NuclearData {
    static constexpr int NUM_SPECIES = 3, ODE_NEQ = 4;
    ARCH_HOST_DEVICE static double aion(int i) { return i == 2 ? 2.0 : 1.0; }
    ARCH_HOST_DEVICE static double zion(int i) { return i == 0 ? 0.0 : 1.0; }
    ARCH_HOST_DEVICE static double spin_weight(int i) { return i == 2 ? 3.0 : 2.0; }
    ARCH_HOST_DEVICE static double binding_energy(int i) { return i == 2 ? 2.224566 : 0.0; }
    ARCH_HOST_DEVICE static double nse_mass_number(int i) {
        return i == 0 ? 1.00866491595 : (i == 1 ? 1.00782503223 : 2.01410177812);
    }
    ARCH_HOST_DEVICE static double energy_weight(int i) {
        return NSE_ATOMIC_MASS_UNIT * (nse_mass_number(i) - aion(i));
    }
};

struct ConstantPartition : LightNetwork {
    ARCH_HOST_DEVICE static double nse_log_partition(int i, double) {
        return i == 2 ? 0.693147180559945309417232121458176568 : 0.0;
    }
};

struct AlphaNetwork : NuclearData {
    static constexpr int NUM_SPECIES = 2, ODE_NEQ = 3;
    ARCH_HOST_DEVICE static double aion(int i) { return i == 0 ? 2.0 : 4.0; }
    ARCH_HOST_DEVICE static double zion(int i) { return i == 0 ? 1.0 : 2.0; }
    ARCH_HOST_DEVICE static double spin_weight(int i) { return i == 0 ? 3.0 : 1.0; }
    ARCH_HOST_DEVICE static double binding_energy(int i) { return i == 0 ? 2.224566 : 28.29566; }
    ARCH_HOST_DEVICE static double nse_mass_number(int i) { return i == 0 ? 2.01410177812 : 4.00260325413; }
    ARCH_HOST_DEVICE static double energy_weight(int i) {
        return NSE_ATOMIC_MASS_UNIT * (nse_mass_number(i) - aion(i));
    }
};

struct ShiftedMassGauge : LightNetwork {
    ARCH_HOST_DEVICE static double energy_weight(int i) {
        return LightNetwork::energy_weight(i) + aion(i) * 1.0e-26;
    }
};

struct InvalidMass : LightNetwork {
    ARCH_HOST_DEVICE static double nse_mass_number(int) { return -1.0; }
};

struct InvalidPartition : LightNetwork {
    ARCH_HOST_DEVICE static double nse_log_partition(int, double) {
        return std::numeric_limits<double>::quiet_NaN();
    }
};

struct ConstantCv {
    double cv = 1.0e9;
    ARCH_HOST_DEVICE double get_eint_from_T(double, double temperature, const double*) const {
        return cv * temperature;
    }
    ARCH_HOST_DEVICE double get_cv(double, double, const double*) const { return cv; }
};

ARCH_HOST_DEVICE inline BurnConfigView configuration()
{
    BurnConfigView config{};
    config.use_nse = true;
    config.smallt = 1.0e5;
    config.nseTempThreshold = 4.5e9;
    config.nseDensThreshold = 1.0e6;
    return config;
}

ARCH_HOST_DEVICE inline bool close(double actual, double expected,
                                    double absolute = 1.0e-12)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= absolute;
}

ARCH_HEAVY_INLINE bool rank_two()
{
    const double old[3]{0.5, 0.5, 0.0};
    double output[3]{}, energy = 0.0;
    if (!NSESolver<LightNetwork>::solve(5.0e9, 1.0e7, 0.5, old, output, energy)) return false;
    const double expected_energy = OdeMath::integrated_composition_energy<LightNetwork>(output, old);
    return close(output[0], GeneratedNseReference::neutron_proton)
        && close(output[1], GeneratedNseReference::neutron_proton)
        && close(output[2], GeneratedNseReference::deuteron)
        && energy > 0.0 && energy == expected_energy;
}

ARCH_HEAVY_INLINE bool rank_one()
{
    const double old[2]{0.5, 0.5};
    double output[2]{}, energy = 0.0;
    if (!NSESolver<AlphaNetwork>::solve(1.0e10, 1.0e9, 0.5, old, output, energy)) return false;
    return close(output[0], GeneratedNseReference::alpha_deuteron)
        && close(output[1], GeneratedNseReference::alpha_helium)
        && energy == OdeMath::integrated_composition_energy<AlphaNetwork>(output, old);
}

ARCH_HEAVY_INLINE bool partition_and_fixed_point()
{
    const double old[3]{0.5, 0.5, 0.0};
    double output[3]{}, energy = 0.0;
    if (!NSESolver<ConstantPartition>::solve(5.0e9, 1.0e7, 0.5, old, output, energy)) return false;
    if (!close(output[0], GeneratedNseReference::partition_neutron_proton)
        || !close(output[1], GeneratedNseReference::partition_neutron_proton)) return false;
    double again[3]{};
    if (!NSESolver<ConstantPartition>::solve(5.0e9, 1.0e7, 0.5, output, again, energy)
        || energy != 0.0) return false;
    for (int i = 0; i < 3; ++i) if (again[i] != output[i]) return false;
    // A density change must invalidate the equilibrium certificate.
    if (!NSESolver<LightNetwork>::solve(5.0e9, 2.0e7, 0.5, old, again, energy)) return false;
    return close(again[0], GeneratedNseReference::partition_neutron_proton)
        && energy > 0.0;
}

ARCH_HEAVY_INLINE bool energy_closure()
{
    const double initial[4]{0.5, 0.5, 0.0, 5.0e9};
    double state[4]{0.5, 0.5, 0.0, 5.0e9};
    double dt = 0.0, energy = 0.0;
    const ConstantCv eos{};
    const auto config = configuration();
    if (!OdeMath::integrate_nse_state<LightNetwork>(
            state, 1.0e7, 1.0e-4, eos, config, dt, &energy)) return false;
    const double delta = eos.cv * (state[3] - initial[3]);
    if (dt != 1.0e-4 || energy <= 0.0 || state[3] <= initial[3]
        || std::abs(delta - energy) > 1.0e-12 * eos.cv * state[3]) return false;
    const double source = OdeMath::integrated_composition_energy<LightNetwork>(state, initial);
    if (energy != source) return false;
    const double before[4]{state[0], state[1], state[2], state[3]};
    if (!OdeMath::integrate_nse_state<LightNetwork>(
            state, 1.0e7, 1.0e-20, eos, config, dt, &energy) || energy != 0.0) return false;
    for (int i = 0; i < 4; ++i) if (state[i] != before[i]) return false;
    return true;
}

template <class Network>
ARCH_HEAVY_INLINE bool rejects_without_output_change(double temperature)
{
    const double old[3]{0.5, 0.5, 0.0};
    double output[3]{3.0, 4.0, 5.0}, energy = 17.0;
    return !NSESolver<Network>::solve(temperature, 1.0e7, 0.5, old, output, energy)
        && output[0] == 3.0 && output[1] == 4.0 && output[2] == 5.0 && energy == 0.0;
}

ARCH_HEAVY_INLINE bool failure_preservation()
{
    if (!rejects_without_output_change<LightNetwork>(1.1e10)
        || !rejects_without_output_change<LightNetwork>(1.0e7)
        || !rejects_without_output_change<InvalidMass>(5.0e9)
        || !rejects_without_output_change<InvalidPartition>(5.0e9)) return false;
    const double invalid[3][3]{{0.6, 0.6, 0.0}, {0.5, 0.5, 0.0},
                                {-1.0e-13, 0.5 + 1.0e-13, 0.5}};
    const double charges[3]{0.6, 0.4, 0.75 + 1.0e-13};
    for (int i = 0; i < 3; ++i) {
        double output[3]{3.0, 4.0, 5.0}, source = 17.0;
        if (NSESolver<LightNetwork>::solve(5.0e9, 1.0e7, charges[i],
                                           invalid[i], output, source)
            || output[0] != 3.0 || output[1] != 4.0 || output[2] != 5.0
            || source != 0.0) return false;
    }
    // Dissociation needs a solution below the selected NSE entry temperature.
    // The projection must leave the input for ordinary kinetic evolution.
    double state[4]{0.0, 0.0, 1.0, 4.500000001e9};
    const double before[4]{state[0], state[1], state[2], state[3]};
    double dt = 11.0, energy = 13.0;
    if (OdeMath::integrate_nse_state<LightNetwork>(
            state, 1.0e7, 1.0e-4, ConstantCv{}, configuration(), dt, &energy)) return false;
    for (int i = 0; i < 4; ++i) if (state[i] != before[i]) return false;
    return dt == 11.0 && energy == 13.0;
}

ARCH_HOST_DEVICE inline bool mass_gauge()
{
    const double increment[3]{-0.125, -0.125, 0.25};
    const double original = OdeMath::composition_increment_energy<LightNetwork>(increment);
    const double shifted = OdeMath::composition_increment_energy<ShiftedMassGauge>(increment);
    return std::isfinite(original) && original > 0.0
        && std::abs(shifted - original) <= 1.0e-12 * std::abs(original);
}

struct Results {
    bool rank_two, rank_one, partition, energy, rejection, gauge;
    ARCH_HOST_DEVICE bool passed() const {
        return rank_two && rank_one && partition && energy && rejection && gauge;
    }
};

ARCH_HEAVY_INLINE Results run()
{
    return {rank_two(), rank_one(), partition_and_fixed_point(),
            energy_closure(), failure_preservation(), mass_gauge()};
}

} // namespace arch::test::generated_nse
