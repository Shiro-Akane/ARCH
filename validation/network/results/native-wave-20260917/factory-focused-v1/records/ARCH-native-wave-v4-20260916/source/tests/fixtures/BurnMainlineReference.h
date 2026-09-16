#pragma once

#include "data/GlobalDefs.h"
#include "numerics/linalg/DenseWrap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

// Immutable test DATA, not a second implementation of the burn physics.
// Source: frozen main 8c76be85e2abc8b4a58049767e9ef32b34d6754b, extracted
// with git archive; only its production HelmEos/network/ODE headers evaluated.
// Recorded 2026-09-05, x86_64 GCC 12.4.0, -std=c++20 -O0
// -fno-fast-math -ffp-contract=off, ARCH_HAS_KLU=0 (DenseLUSolver).
// Reproduction: run the twelve check_route integrations below against those
// frozen headers using make_config/make_state and record hexadecimal doubles.
// There is deliberately no --update/auto-refresh mode. A reference change
// requires independent review of the frozen source and table identity.
namespace BurnMainlineReference {

inline constexpr char kMainCommit[] = "8c76be85e2abc8b4a58049767e9ef32b34d6754b";
inline constexpr char kTableSha256[] =
    "c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1";
inline constexpr double kRho = 1.0e6;
// Unlike the 1e-16 policy fixture, this duration produces a burn signal large
// enough that a no-op and a 1% energy-release change must both be rejected.
inline constexpr double kDt = 1.0e-12;

inline BurnConfig make_config()
{
    BurnConfig config{};
    config.use_burn = true;
    config.use_nse = false;
    config.nuclearTempMin = 1.0e8;
    config.nuclearDensMin = 1.0;
    config.smallt = 1.0e5;
    config.smallx = 1.0e-30;
    config.enucDtFactor = 0.5;
    config.odeconfig.rtol = 1.0e-4;
    config.odeconfig.atol = 1.0e-8;
    config.odeconfig.max_newton_iter = 50;
    config.odeconfig.max_substeps = 100;
    config.odeconfig.initial_dt_frac = 1.0;
    config.odeconfig.dt_safe_factor = 0.9;
    config.odeconfig.dt_fac_min = 0.1;
    config.odeconfig.dt_fac_max = 2.0;
    return config;
}

template<class Net>
std::array<double, BurnLimits::MAX_ODE_NEQ> make_state(int variant)
{
    std::array<double, BurnLimits::MAX_ODE_NEQ> state{};
    const double denominator =
        static_cast<double>(Net::NUM_SPECIES * (Net::NUM_SPECIES + 1) / 2);
    double sum = 0.0;
    std::uint32_t seed = 0x9e3779b9U ^ static_cast<std::uint32_t>(variant + 1);
    for (int i = 0; i < Net::NUM_SPECIES; ++i) {
        seed = 1664525U * seed + 1013904223U;
        const double perturbation = 1.0
            + 1.0e-5 * static_cast<double>(static_cast<int>(seed % 9U) - 4);
        state[i] = static_cast<double>(i + 1) / denominator * perturbation;
        sum += state[i];
    }
    for (int i = 0; i < Net::NUM_SPECIES; ++i) state[i] /= sum;
    state[Net::ODE_NEQ - 1] = 2.0e9 + 1.0e6 * variant;
    return state;
}

struct Reference {
    const char* name;
    int species;
    int variant;
    double dt_recommended;
    double eint_old;
    double eint_new;
    std::array<double, BurnLimits::MAX_ODE_NEQ> state;
};

inline constexpr Reference kRoutes[]{
    {"aprox13.be_nr", 13, 0, 0x1.faa7ab552a552p-40, 0x1.1413e4687cfdfp+58, 0x1.1413e46d0deccp+58,
        {0x1.6814e75a4ea66p-7, 0x1.681a6f98a96bep-6, 0x1.0e13d3aae6e56p-5, 0x1.6815d36ffab12p-5,
         0x1.c217d3d1153e3p-5, 0x1.0e0efcd23b9e3p-4, 0x1.3b1584ab637ebp-4, 0x1.6815d3a8da17ep-4,
         0x1.951aa11696069p-4, 0x1.c21fe482c81bfp-4, 0x1.ef1a359437d4bp-4, 0x1.0e11c0b9884e3p-3,
         0x1.2491bbf8b84eap-3, 0x1.dcd6500332f19p+30}},
    {"aprox13.bd", 13, 0, 0x1.faa7ab552a552p-40, 0x1.1413e4687cfdfp+58, 0x1.1413e46d0df15p+58,
        {0x1.6814e75a4ea66p-7, 0x1.681a6f98a96bep-6, 0x1.0e13d3aae6e56p-5, 0x1.6815d36ffab1p-5,
         0x1.c217d3d1153e3p-5, 0x1.0e0efcd23b9e3p-4, 0x1.3b1584ab637ebp-4, 0x1.6815d3a8da18p-4,
         0x1.951aa11696069p-4, 0x1.c21fe482c81bfp-4, 0x1.ef1a359437d4cp-4, 0x1.0e11c0b9884e2p-3,
         0x1.2491bbf8b84e8p-3, 0x1.dcd6500332f1cp+30}},
    {"aprox13.ros4", 13, 0, 0x1.faa7ab552a552p-40, 0x1.1413e4687cfdfp+58, 0x1.1413e46d0deccp+58,
        {0x1.6814e75a4ea66p-7, 0x1.681a6f98a96bep-6, 0x1.0e13d3aae6e56p-5, 0x1.6815d36ffab12p-5,
         0x1.c217d3d1153e4p-5, 0x1.0e0efcd23b9e3p-4, 0x1.3b1584ab637ebp-4, 0x1.6815d3a8da17ep-4,
         0x1.951aa11696069p-4, 0x1.c21fe482c81cp-4, 0x1.ef1a359437d4ap-4, 0x1.0e11c0b9884e3p-3,
         0x1.2491bbf8b84eap-3, 0x1.dcd6500332f19p+30}},
    {"aprox19.be_nr", 19, 1, 0x1.faa7ab552a552p-40, 0x1.44a49f21491ffp+58, 0x1.47af8b5b779d3p+58,
        {0x1.5bbbe896a7d87p-8, 0x1.54b9e4c12f9a9p-7, 0x1.0581652b08503p-6, 0x1.58e67d3b5b3ffp-6,
         0x1.af28f5b8bba15p-6, 0x1.02b38afe84aa1p-5, 0x1.2dcf27491d8dap-5, 0x1.58eda0363c935p-5,
         0x1.840c526335ce8p-5, 0x1.af2a2280dc80ap-5, 0x1.da49299217a48p-5, 0x1.02b237f73271ep-4,
         0x1.183cc3fab585ep-4, 0x1.2dcd9a9561455p-4, 0x1.2aedbefa95b94p-4, 0x1.724f71395d2a3p-4,
         0x1.6e7c5044e2e42p-4, 0x1.82eda266771f9p-4, 0x1.996d1cf60d3f9p-4, 0x1.df06818c0a032p+30}},
    {"aprox19.bd", 19, 1, 0x1.3656a3d670d2cp-40, 0x1.44a49f21491ffp+58, 0x1.47d01b4c7d55p+58,
        {0x1.5bbfcd8c720bep-8, 0x1.54b40ff769f3p-7, 0x1.058c826630818p-6, 0x1.58e67fe24b087p-6,
         0x1.af28f2a845f34p-6, 0x1.02b38afea910cp-5, 0x1.2dcf2745249bep-5, 0x1.58eda034ef33ep-5,
         0x1.840c5262771ebp-5, 0x1.af2a2280d88b3p-5, 0x1.da4929920ea63p-5, 0x1.02b237f72fe66p-4,
         0x1.183cc3fab56e2p-4, 0x1.2dcd9a956146dp-4, 0x1.29f1690549d65p-4, 0x1.735583f8a28efp-4,
         0x1.6e7c47b61aa82p-4, 0x1.82e2c8693e51cp-4, 0x1.996bf7d081aeap-4, 0x1.df1b37d1c69dp+30}},
    {"aprox19.ros4", 19, 1, 0x1.cdbe032d6b587p-40, 0x1.44a49f21491ffp+58, 0x1.47d07af662788p+58,
        {0x1.5bbfcecdc43a3p-8, 0x1.54b40e153d0aep-7, 0x1.058c80d9a4069p-6, 0x1.58e67fe21166dp-6,
         0x1.af28f2a887b9p-6, 0x1.02b38afea9055p-5, 0x1.2dcf27452543ap-5, 0x1.58eda034ef71ap-5,
         0x1.840c5262773f8p-5, 0x1.af2a2280d8832p-5, 0x1.da4929920e9e6p-5, 0x1.02b237f72fe18p-4,
         0x1.183cc3fab5685p-4, 0x1.2dcd9a9561409p-4, 0x1.29f16e1bc3498p-4, 0x1.73557eaf4d0d9p-4,
         0x1.6e7c47b6e5826p-4, 0x1.82e2c8e0ff4fep-4, 0x1.996bf816230ffp-4, 0x1.df1b737fe3be6p+30}},
    {"aprox21.be_nr", 21, 2, 0x1.faa7ab552a552p-40, 0x1.3dd63fe72380ep+58, 0x1.4a105d4391c95p+58,
        {0x1.1db2a5dd77832p-8, 0x1.18b4f29c9840fp-7, 0x1.ad62a9a0c1136p-7, 0x1.1bb0ecfa711cdp-6,
         0x1.62a030103c701p-6, 0x1.a991cf3160711p-6, 0x1.f07ba34558451p-6, 0x1.1bb7444a3df17p-5,
         0x1.3f2e2cb65cfd9p-5, 0x1.62a42cb66e469p-5, 0x1.8617ff079aeb2p-5, 0x1.a98b45e599d53p-5,
         0x1.cd05161f2275ep-5, 0x1.f07ba1ab5b111p-5, 0x1.09f65d6056433p-4, 0x1.0842e7b8081a2p-4,
         0x1.7b4d7e291312ap-5, 0x1.c80ac53d2ea23p-4, 0x1.50e3fb658a006p-4, 0x1.5ce330047f08p-4,
         0x1.74417c2ea67dfp-4, 0x1.e516f87947ba2p+30}},
    {"aprox21.bd", 21, 2, 0x1.aecb052eb2ad7p-40, 0x1.3dd63fe72380ep+58, 0x1.4c7705d129ee3p+58,
        {0x1.1dae265913234p-8, 0x1.18bbb483045bdp-7, 0x1.ad83180726a45p-7, 0x1.1bb0ef99f4191p-6,
         0x1.62a02d0c866aep-6, 0x1.a991cf31c91dap-6, 0x1.f07ba339b87c8p-6, 0x1.1bb744480e6a4p-5,
         0x1.3f2e2cb511f0fp-5, 0x1.62a42cb667555p-5, 0x1.8617ff078992bp-5, 0x1.a98b45e58f42cp-5,
         0x1.cd05161f21a23p-5, 0x1.f07ba1ab5a98dp-5, 0x1.09f65d6056094p-4, 0x1.07735f468e228p-4,
         0x1.49a3074ad5e2cp-5, 0x1.e2aae59e4bf5ep-4, 0x1.50e409ad4d603p-4, 0x1.5be576c4f1971p-4,
         0x1.743f2cb241fcp-4, 0x1.e691597da250ap+30}},
    {"aprox21.ros4", 21, 2, 0x1.12de632e5d8e9p-42, 0x1.3dd63fe72380ep+58, 0x1.4c797b062717ap+58,
        {0x1.1dae2a1348395p-8, 0x1.18bbaeeae30dap-7, 0x1.ad831005ab7e9p-7, 0x1.1bb0ef9964194p-6,
         0x1.62a02d0d2af7ap-6, 0x1.a991cf31c944ep-6, 0x1.f07ba339bc92cp-6, 0x1.1bb744480f5f4p-5,
         0x1.3f2e2cb5129e8p-5, 0x1.62a42cb66794ap-5, 0x1.8617ff0789dbfp-5, 0x1.a98b45e58f8fcp-5,
         0x1.cd05161f21f17p-5, 0x1.f07ba1ab5aee4p-5, 0x1.09f65d605636fp-4, 0x1.07735fbd86c7dp-4,
         0x1.49a1385113434p-5, 0x1.e2abd52c1eee6p-4, 0x1.50e409af160b5p-4, 0x1.5be56ef7137b2p-4,
         0x1.743f2d6df56ccp-4, 0x1.e692d28ad783p+30}},
    {"iso7.be_nr", 7, 3, 0x1.faa7ab552a552p-40, 0x1.19fb3f2f86bb4p+58, 0x1.19fb3f5b314ddp+58,
        {0x1.2491aae37f2a2p-5, 0x1.2491aba44b22cp-4, 0x1.b6dde0231086p-4, 0x1.2494aa063ca4fp-3,
         0x1.6db7f682e2908p-3, 0x1.b6d8427aa0bd8p-3, 0x1.fffeec5fb26e4p-3, 0x1.dd8d6b1e17702p+30}},
    {"iso7.bd", 7, 3, 0x1.faa7ab552a552p-40, 0x1.19fb3f2f86bb4p+58, 0x1.19fb3f5b314d7p+58,
        {0x1.2491aae37f2a1p-5, 0x1.2491aba44b22dp-4, 0x1.b6dde02310862p-4, 0x1.2494aa063ca4bp-3,
         0x1.6db7f682e290bp-3, 0x1.b6d8427aa0bd9p-3, 0x1.fffeec5fb26e4p-3, 0x1.dd8d6b1e177p+30}},
    {"iso7.ros4", 7, 3, 0x1.faa7ab552a552p-40, 0x1.19fb3f2f86bb4p+58, 0x1.19fb3f5b31509p+58,
        {0x1.2491aae37f2ap-5, 0x1.2491aba44b22dp-4, 0x1.b6dde0231085fp-4, 0x1.2494aa063ca4cp-3,
         0x1.6db7f682e290dp-3, 0x1.b6d8427aa0bd8p-3, 0x1.fffeec5fb26e4p-3, 0x1.dd8d6b1e17703p+30}},
};

inline const Reference& find(std::string_view name)
{
    for (const auto& route : kRoutes)
        if (route.name == name) return route;
    throw std::runtime_error("unknown frozen-main burn route: " + std::string(name));
}

// Fixed acceptance criteria, independent of measured CPU/CUDA differences.
// State drift may consume at most 1e-7 of the configured ODE weighted RMS
// error norm: seven orders below the integrator's own local error scale.
// This is an acceptance contract, not a claim of a rigorous global error bound.
inline double state_budget(double expected)
{
    const auto config = make_config();
    return 1.0e-7 * (config.odeconfig.atol
        + config.odeconfig.rtol * std::abs(expected));
}

inline void require_absolute(double actual, double expected, double budget,
                             const std::string& label)
{
    if (!std::isfinite(actual) || !std::isfinite(expected)
        || std::abs(actual - expected) > budget)
        throw std::runtime_error(label + " drifted from frozen main " + kMainCommit);
}

inline void validate(const Reference& reference, const double* state,
                     int species, double dt_recommended,
                     double eint_old, double eint_new)
{
    const std::string label = std::string(reference.name) + ".mainline";
    if (species != reference.species)
        throw std::runtime_error(label + " species extent changed");
    double mass = 0.0;
    double scaled_error_squares = 0.0;
    const double component_envelope = std::sqrt(static_cast<double>(species + 1));
    for (int i = 0; i <= species; ++i) {
        require_absolute(state[i], reference.state[i],
                         component_envelope * state_budget(reference.state[i]),
                         label + ".state." + std::to_string(i));
        const double scaled_error = (state[i] - reference.state[i])
            / state_budget(reference.state[i]);
        scaled_error_squares += scaled_error * scaled_error;
        if (i < species) {
            if (state[i] < 0.0)
                throw std::runtime_error(label + " has negative mass fraction");
            mass += state[i];
        }
    }
    if (std::sqrt(scaled_error_squares / (species + 1)) > 1.0)
        throw std::runtime_error(label + " weighted RMS state drift exceeded contract");
    constexpr double eps = std::numeric_limits<double>::epsilon();
    require_absolute(mass, 1.0, 64.0 * eps, label + ".mass");
    // Adaptive error estimates amplify roundoff near their subtraction floor.
    // A sqrt(epsilon)-scale allowance is solely for the next-step suggestion,
    // not the integrated state; use a RELATIVE scale even for subsecond dt.
    require_absolute(dt_recommended, reference.dt_recommended,
        16.0 * std::sqrt(eps) * std::abs(reference.dt_recommended), label + ".dt");
    if (!(dt_recommended > 0.0))
        throw std::runtime_error(label + " nonpositive recommended dt");
    // EOS interpolation and subtracting two ~1e17 energies need a roundoff
    // floor. Freeze 4096 eps of their scale; the explicit delta check prevents
    // two individually accepted EOS errors from masking lost burn heating.
    const double energy_budget = 4096.0 * eps
        * std::max(std::abs(reference.eint_old), std::abs(reference.eint_new));
    require_absolute(eint_old, reference.eint_old, energy_budget, label + ".eint_old");
    require_absolute(eint_new, reference.eint_new, energy_budget, label + ".eint_new");
    require_absolute(eint_new - eint_old,
        reference.eint_new - reference.eint_old, energy_budget, label + ".delta_eint");
    if (!(eint_new > eint_old))
        throw std::runtime_error(label + " lost the positive burn-energy signal");
}

template<class Net, template<class, class, class> class Solver, class Eos>
void check_route(const char* name, int variant, const Eos& eos)
{
    const auto& reference = find(name);
    if (variant != reference.variant)
        throw std::runtime_error(std::string(name) + " input variant changed");
    auto state = make_state<Net>(variant);
    double dt_recommended = kDt;
    const double eint_old = eos.get_eint_from_T(
        kRho, state[Net::ODE_NEQ - 1], state.data());
    if (!Solver<Net, DenseMatrixData<Net::ODE_NEQ>, DenseLUSolver>::integrate(
            state.data(), kRho, kDt, eos, make_config(), dt_recommended))
        throw std::runtime_error(std::string(name) + " mainline integration failed");
    const double eint_new = eos.get_eint_from_T(
        kRho, state[Net::ODE_NEQ - 1], state.data());
    validate(reference, state.data(), Net::NUM_SPECIES,
             dt_recommended, eint_old, eint_new);
}

} // namespace BurnMainlineReference
