#pragma once

#include "BurnMainlineReference.h"

#include <iostream>

// Immutable endpoints from independent DOP853 time integration, corroborated
// by Radau. Reaction rates and the Helm EOS use the production models:
// this is NOT an independent nuclear-data reference. Reproduce/read-only check:
// validation/burn/time_reference.py --binary <arch_burn_mainline_reference>.
// Method-dependent historical snapshots are preserved in BurnMainlineReference.h;
// they describe implementation history rather than the converged time solution.
// Fixed-density first-law endpoints independently derived in run-656: DOP853
// and Radau at two resolutions, with a passive energy-rate quadrature and a
// separate 60/80-digit Helm energy balance. This includes the EOS composition
// energy term; a q/cv-only trajectory omits that first-law contribution.
namespace BurnTimeReference {

using BurnMainlineReference::kDt;
using BurnMainlineReference::kRho;
using BurnMainlineReference::make_state;

struct Reference {
    const char* network;
    int species, variant;
    std::array<double, BurnLimits::MAX_ODE_NEQ> state;
};

inline constexpr Reference kEndpoints[]{
    {"aprox13", 13, 0,
        {0x1.6814e75a4ea66p-7, 0x1.681a6f98a96bep-6, 0x1.0e13d3aae6e56p-5,
         0x1.6815d36ffab12p-5, 0x1.c217d3d1153e3p-5, 0x1.0e0efcd23b9e4p-4,
         0x1.3b1584ab637ebp-4, 0x1.6815d3a8da17ep-4, 0x1.951aa11696068p-4,
         0x1.c21fe482c81bfp-4, 0x1.ef1a359437d4bp-4, 0x1.0e11c0b9884e4p-3,
         0x1.2491bbf8b84eap-3, 0x1.dcd6500353998p+30}},
    {"aprox19", 19, 1,
        {0x1.5bbfe8cc9f9c0p-8, 0x1.54b3e712c3f2fp-7, 0x1.058c606574e9fp-6,
         0x1.58e67fdde38b1p-6, 0x1.af28f2ad4d2d0p-6, 0x1.02b38afea8917p-5,
         0x1.2dcf27453265bp-5, 0x1.58eda034f488fp-5, 0x1.840c52627a3e6p-5,
         0x1.af2a2280d8954p-5, 0x1.da4929920ec86p-5, 0x1.02b237f72feeap-4,
         0x1.183cc3fab56a5p-4, 0x1.2dcd9a9561425p-4, 0x1.29f177e8f1ddbp-4,
         0x1.7355747632b8bp-4, 0x1.6e7c47c2bbb6fp-4, 0x1.82e2cef01f75dp-4,
         0x1.996bfdc45532dp-4, 0x1.df2548920c5fcp+30}},
    {"aprox21", 21, 2,
        {0x1.1dae9a7280226p-8, 0x1.18bb064b20f36p-7, 0x1.ad820ef17aad2p-7,
         0x1.1bb0ef888753bp-6, 0x1.62a02d2062c42p-6, 0x1.a991cf31c568ep-6,
         0x1.f07ba33a2d15ap-6, 0x1.1bb744482625ap-5, 0x1.3f2e2cb52055ap-5,
         0x1.62a42cb667e99p-5, 0x1.8617ff078aa83p-5, 0x1.a98b45e590156p-5,
         0x1.cd05161f220b4p-5, 0x1.f07ba1ab5b055p-5, 0x1.09f65d6056430p-4,
         0x1.077387e190ac4p-4, 0x1.49a081cbf0ef5p-5, 0x1.e2ac086e525ffp-4,
         0x1.50e409d63aee4p-4, 0x1.5be585c58b48cp-4, 0x1.743f4484724bap-4,
         0x1.e6ca093d209d3p+30}},
    {"iso7", 7, 3,
        {0x1.2491aae37f2a0p-5, 0x1.2491aba44b22cp-4, 0x1.b6dde0231085fp-4,
         0x1.2494aa063ca4cp-3, 0x1.6db7f682e290cp-3, 0x1.b6d8427aa0bd7p-3,
         0x1.fffeec5fb26e4p-3, 0x1.dd8d6b1f1aed9p+30}},
};

inline const Reference& find(std::string_view network)
{
    for (const auto& reference : kEndpoints)
        if (reference.network == network) return reference;
    throw std::runtime_error("unknown burn time reference: " + std::string(network));
}

inline BurnConfig make_config()
{
    auto config = BurnMainlineReference::make_config();
    // Accuracy qualification, not a change to the application's defaults.
    // First-order BE needs a tighter local budget than the high-order methods
    // for a common global target. Use the same strict input for all three.
    config.odeconfig.rtol = 1.0e-13;
    config.odeconfig.atol = 1.0e-17;
    config.odeconfig.max_substeps = 3000000;
    return config;
}

struct Errors {
    double species_linf = 0.0, relative_temperature = 0.0;
    double relative_energy = 0.0, mass_closure = 0.0;
};

template<class Eos>
Errors validate(const Reference& reference, const double* state,
                double dt_recommended, double old_energy, double new_energy,
                const Eos& eos)
{
    const std::string label = std::string(reference.network) + ".time_reference";
    if (!std::isfinite(dt_recommended) || !(dt_recommended > 0.0)
        || !std::isfinite(old_energy) || !std::isfinite(new_energy))
        throw std::runtime_error(label + " nonfinite energy or invalid next step");
    Errors errors;
    double mass = 0.0;
    for (int i = 0; i <= reference.species; ++i) {
        if (!std::isfinite(state[i]) || state[i] < 0.0)
            throw std::runtime_error(label + " invalid state");
        if (i < reference.species) {
            mass += state[i];
            errors.species_linf = std::max(errors.species_linf,
                std::abs(state[i] - reference.state[i]));
        }
    }
    errors.mass_closure = std::abs(mass - 1.0);
    errors.relative_temperature = std::abs(
        state[reference.species] / reference.state[reference.species] - 1.0);
    const double expected_energy = eos.get_eint_from_T(
        kRho, reference.state[reference.species], reference.state.data());
    if (!std::isfinite(expected_energy) || !(expected_energy > old_energy))
        throw std::runtime_error(label + " lost the reference heating signal");
    errors.relative_energy = std::abs(new_energy / expected_energy - 1.0);
    // Burn validation uses 1e-8 species/total-energy and 1e-12 closure gates
    // against the independent time-integration reference.
    // Temperature has the same dimensionless target. Also resolve the heating
    // signal to 1% when total-energy scaling would otherwise accept a no-op.
    if (errors.species_linf > 1.0e-8 || errors.relative_energy > 1.0e-8
        || errors.relative_temperature > 1.0e-8 || errors.mass_closure > 1.0e-12
        || std::abs(new_energy - expected_energy)
            > 0.01 * (expected_energy - old_energy))
        throw std::runtime_error(label + " exceeded scientific accuracy budget");
    return errors;
}

template<class Net, template<class, class, class> class Solver, class Eos>
void check_route(const char* name, int variant, const Eos& eos)
{
    const auto& reference = find(Net::NETWORK_NAME);
    if (reference.species != Net::NUM_SPECIES || reference.variant != variant)
        throw std::runtime_error("burn time reference input mismatch");
    auto state = make_state<Net>(variant);
    const double old_energy = eos.get_eint_from_T(kRho, state[Net::NUM_SPECIES], state.data());
    double dt = kDt;
    if (!Solver<Net, DenseMatrixData<Net::ODE_NEQ>, DenseLUSolver>::integrate(
            state.data(), kRho, kDt, eos, make_config(), dt))
        throw std::runtime_error(std::string(name) + " accurate integration failed");
    const double new_energy = eos.get_eint_from_T(kRho, state[Net::NUM_SPECIES], state.data());
    const auto error = validate(reference, state.data(), dt, old_energy, new_energy, eos);
    std::cout << "BURN_TIME_PASS " << name << " species_linf=" << error.species_linf
              << " relative_temperature=" << error.relative_temperature
              << " relative_energy=" << error.relative_energy
              << " mass_closure=" << error.mass_closure << '\n';
}

} // namespace BurnTimeReference
