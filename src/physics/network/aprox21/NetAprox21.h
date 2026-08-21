// C++ adaptation of Frank Timmes's public_aprox21.f90 network.
// ARCH supplies the policy interface; the rate equations and nuclear data
// trace to https://cococubed.com/code_pages/burn.shtml.
#pragma once

#include <array>

#include "TimmesRateLibrary.h"

#include "../timmes_common/AproxRateAssembly.h"
#include "../timmes_common/Ecapnuc.h"
#include "../timmes_common/TimmesNetworkSupport.h"

namespace timmes_aprox21_detail {

enum Species : int {
    ih1, ihe3, ihe4, ic12, in14, io16, ine20, img24, isi28, is32, iar36,
    ica40, iti44, icr48, icr56, ife52, ife54, ife56, ini56, ineut, iprot
};

#define TIMMES_APROX21_RATES(X) \
    X(ir3a) X(irg3a) X(ircag) X(ir1212) X(ir1216) X(ir1616) X(iroga) \
    X(iroag) X(irnega) X(irneag) X(irmgga) X(irmgag) X(irsiga) \
    X(irmgap) X(iralpa) X(iralpg) X(irsigp) X(irsiag) X(irsga) X(irsiap) \
    X(irppa) X(irppg) X(irsgp) X(irsag) X(irarga) X(irsap) X(irclpa) \
    X(irclpg) X(irargp) X(irarag) X(ircaga) X(irarap) X(irkpa) X(irkpg) \
    X(ircagp) X(ircaag) X(irtiga) X(ircaap) X(irscpa) X(irscpg) X(irtigp) \
    X(irtiag) X(ircrga) X(irtiap) X(irvpa) X(irvpg) X(ircrgp) X(ircrag) \
    X(irfega) X(ircrap) X(irmnpa) X(irmnpg) X(irfegp) X(irfeag) X(irniga) \
    X(irfeap) X(ircopa) X(ircopg) X(irnigp) X(ir52ng) X(ir53gn) X(ir53ng) \
    X(ir54gn) X(irfepg) X(ircogp) X(irheng) X(irhegn) X(irhng) X(irdgn) \
    X(irdpg) X(irhegp) X(irpen) X(irnep) X(irn56ec) X(irpp) X(ir33) \
    X(irhe3ag) X(ircpg) X(irnpg) X(ifa) X(ifg) X(iropg) X(irnag) \
    X(ir54ng) X(ir55gn) X(ir55ng) X(ir56gn) X(irfe54ap) X(irco57pa) \
    X(irfe56pg) X(irco57gp) X(irr1) X(irs1) X(irt1) X(iru1) X(irv1) \
    X(irw1) X(irx1) X(ir1f54) X(ir2f54) X(ir3f54) X(ir4f54) X(ir5f54) \
    X(ir6f54) X(ir7f54) X(ir8f54) X(iralf1) X(iralf2) X(irfe56_aux1) \
    X(irfe56_aux2) X(irfe56_aux3) X(irfe56_aux4)

enum Rates : int {
#define X(name) name,
    TIMMES_APROX21_RATES(X)
#undef X
    nrat
};

struct RateIds {
#define X(name) static constexpr int name = timmes_aprox21_detail::name;
    TIMMES_APROX21_RATES(X)
#undef X
};

inline constexpr double sixth = 1.0 / 6.0;

#include "TimmesRhs.inc"
#include "TimmesJacobian.inc"

#undef TIMMES_APROX21_RATES

} // namespace timmes_aprox21_detail

struct NetAprox21 : timmes::TimmesNetworkSupport<NetAprox21> {
    static constexpr int NUM_SPECIES = 21;
    static constexpr int ODE_NEQ = NUM_SPECIES + 1;
    static constexpr const char* NETWORK_NAME = "aprox21";

    inline static constexpr std::array<const char*, NUM_SPECIES> SPECIES_NAMES{
        "h1", "he3", "he4", "c12", "n14", "o16", "ne20", "mg24",
        "si28", "s32", "ar36", "ca40", "ti44", "cr48", "cr56", "fe52",
        "fe54", "fe56", "ni56", "neut", "prot"
    };
    inline static constexpr std::array<double, NUM_SPECIES> AION{
        1.0, 3.0, 4.0, 12.0, 14.0, 16.0, 20.0, 24.0, 28.0, 32.0,
        36.0, 40.0, 44.0, 48.0, 56.0, 52.0, 54.0, 56.0, 56.0, 1.0, 1.0
    };
    inline static constexpr std::array<double, NUM_SPECIES> ZION{
        1.0, 2.0, 2.0, 6.0, 7.0, 8.0, 10.0, 12.0, 14.0, 16.0,
        18.0, 20.0, 22.0, 24.0, 24.0, 26.0, 26.0, 26.0, 28.0, 0.0, 1.0
    };
    inline static constexpr std::array<double, NUM_SPECIES> BION{
        0.0, 7.71819, 28.29603, 92.16294, 104.65998, 127.62093, 160.64788,
        198.25790, 236.53790, 271.78250, 306.72020, 342.05680, 375.47720,
        411.46900, 488.4970, 447.70800, 471.7696, 492.2450, 484.00300, 0.0, 0.0
    };
    inline static constexpr auto BINDING_E = BION;
    // h1 duplicates the free-proton quantum state in this approximate
    // network.  A zero NSE weight keeps the bookkeeping species out of the
    // statistical sum; equilibrium free protons are stored in "prot".
    inline static constexpr std::array<double, NUM_SPECIES> SPIN{
        0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0,
        1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 2.0, 2.0
    };
    inline static constexpr auto MION = timmes::isotope_masses(AION, ZION, BION);
    inline static constexpr auto ENERGY_WEIGHTS = MION;
    static constexpr double ENERGY_CONVERSION = timmes::constants::enuc_conv2;
    static constexpr double NSE_ENERGY_CONVERSION = timmes::constants::enuc_conv;

    // Device-safe scalar accessors. Namespace-scope std::array storage is
    // host-only under NVCC when addressed with a runtime index.
    TIMMES_HD static constexpr double aion(int i)
    {
        switch (i) {
        case 0:  return 1.0;
        case 1:  return 3.0;
        case 2:  return 4.0;
        case 3:  return 12.0;
        case 4:  return 14.0;
        case 5:  return 16.0;
        case 6:  return 20.0;
        case 7:  return 24.0;
        case 8:  return 28.0;
        case 9:  return 32.0;
        case 10: return 36.0;
        case 11: return 40.0;
        case 12: return 44.0;
        case 13: return 48.0;
        case 14: return 56.0;
        case 15: return 52.0;
        case 16: return 54.0;
        case 17: return 56.0;
        case 18: return 56.0;
        case 19: return 1.0;
        case 20: return 1.0;
        default: return 0.0;
        }
    }

    TIMMES_HD static constexpr double zion(int i)
    {
        switch (i) {
        case 0:  return 1.0;
        case 1:  return 2.0;
        case 2:  return 2.0;
        case 3:  return 6.0;
        case 4:  return 7.0;
        case 5:  return 8.0;
        case 6:  return 10.0;
        case 7:  return 12.0;
        case 8:  return 14.0;
        case 9:  return 16.0;
        case 10: return 18.0;
        case 11: return 20.0;
        case 12: return 22.0;
        case 13: return 24.0;
        case 14: return 24.0;
        case 15: return 26.0;
        case 16: return 26.0;
        case 17: return 26.0;
        case 18: return 28.0;
        case 19: return 0.0;
        case 20: return 1.0;
        default: return 0.0;
        }
    }

    template <std::size_t I = 0>
    TIMMES_HD static constexpr double binding_energy(int i)
    {
        if constexpr (I < NUM_SPECIES) {
            return i == static_cast<int>(I) ? BINDING_E[I]
                                             : binding_energy<I + 1>(i);
        }
        return 0.0;
    }

    template <std::size_t I = 0>
    TIMMES_HD static constexpr double spin_weight(int i)
    {
        if constexpr (I < NUM_SPECIES) {
            return i == static_cast<int>(I) ? SPIN[I]
                                             : spin_weight<I + 1>(i);
        }
        return 0.0;
    }

    template <std::size_t I = 0>
    TIMMES_HD static constexpr double energy_weight(int i)
    {
        if constexpr (I < NUM_SPECIES) {
            return i == static_cast<int>(I) ? ENERGY_WEIGHTS[I]
                                             : energy_weight<I + 1>(i);
        }
        return 0.0;
    }

    template <typename Scalar, typename RateAccessor>
    TIMMES_HD static inline void fill_screened_rates(const Scalar* y, double rho,
                                           double eta, double temperature_value,
                                           const Scalar& temperature,
                                           std::array<Scalar,
                                               timmes_aprox21_detail::nrat>& rate)
    {
        using namespace timmes_aprox21_detail;
        if (temperature_value >= 1.0e6) {
            const timmes::TfactorsData tf = timmes::compute_tfactors(temperature_value);
            timmes::fill_heavy_rates<RateIds, timmes::Aprox21RateLibrary, RateAccessor>(
                rate, temperature_value, rho, tf);
            timmes::fill_extended_rates<RateIds, timmes::Aprox21RateLibrary, RateAccessor>(
                rate, temperature_value, rho, tf);
            timmes::fill_aprox21_extra_rates<RateIds, timmes::Aprox21RateLibrary, RateAccessor>(
                rate, temperature_value, rho, tf);

#if defined(__CUDA_ARCH__)
            double zion_values[NUM_SPECIES];
            for (int i = 0; i < NUM_SPECIES; ++i) zion_values[i] = zion(i);
            const double* zion_data = zion_values;
#else
            const double* zion_data = ZION.data();
#endif
            Scalar abar, zbar, z2bar, ye;
            timmes::composition_moments<Scalar, NUM_SPECIES>(
                y, zion_data, abar, zbar, z2bar, ye);
            timmes::screen_heavy_rates<RateIds>(
                rate, temperature, rho, zbar, abar, z2bar);
            timmes::screen_extended_rates<RateIds>(
                rate, temperature, rho, zbar, abar, z2bar);
            timmes::screen_aprox21_extra_rates<RateIds>(
                rate, temperature, rho, zbar, abar, z2bar);

            // ecapnuc supplies the proton-electron and neutron-positron weak rates.
            Scalar rpen, rnep, spenc, snepc;
            timmes::ecapnuc(eta, temperature, rpen, rnep, spenc, snepc);
            rate[RateIds::irpen] = rpen;
            rate[RateIds::irnep] = rnep;
            // Ni56 electron capture remains on the network's dedicated tabular path.
        }
    }

    template <typename Scalar, typename RateAccessor>
    TIMMES_HD static inline void molar_rhs_impl(const Scalar* y, double rho, double eta,
                                      double temperature_value,
                                      const Scalar& temperature, Scalar* dydt)
    {
        using namespace timmes_aprox21_detail;
        std::array<Scalar, nrat> rate{};
        fill_screened_rates<Scalar, RateAccessor>(
            y, rho, eta, temperature_value, temperature, rate);
        timmes::form_extended_equilibrium<true, RateIds, Scalar>(
            rate, y, ihe4, ih1, ineut, iprot, temperature_value);
        rhs_aprox21(y, rate.data(), dydt);
    }

    template <typename Scalar>
    TIMMES_HD static inline void molar_rhs_frozen_screening(
        const Scalar* y, double rho, double eta, double temperature, Scalar* dydt)
    {
        using namespace timmes_aprox21_detail;
        std::array<double, NUM_SPECIES> y_value{};
        for (int i = 0; i < NUM_SPECIES; ++i) {
            y_value[i] = timmes::value_of(y[i]);
        }
        std::array<double, nrat> rate_value{};
        fill_screened_rates<double, timmes::RateValueAccessor>(
            y_value.data(), rho, eta, temperature, temperature, rate_value);
        std::array<Scalar, nrat> rate{};
        for (int i = 0; i < nrat; ++i) rate[i] = Scalar(rate_value[i]);
        timmes::form_extended_equilibrium<true, RateIds, Scalar>(
            rate, y, ihe4, ih1, ineut, iprot, temperature);
        rhs_aprox21(y, rate.data(), dydt);
    }

    /**
     * Analytic molar RHS/Jacobian with Timmes frozen base screening.
     *
     * The generated fixed-rate block covers every direct abundance term.
     * Only four columns can also enter form_extended_equilibrium(); those
     * columns receive a bounded Dual<1> closure chain-rule correction using
     * the already computed frozen base rates. This preserves Timmes closure
     * semantics without carrying Dual<21> through the complete rate library.
     */
#if defined(__CUDACC__)
#  define TIMMES_NETWORK_JAC_NOINLINE __noinline__
#else
#  define TIMMES_NETWORK_JAC_NOINLINE
#endif
    TIMMES_HD TIMMES_NETWORK_JAC_NOINLINE static inline void
    molar_rhs_jacobian_frozen_screening(
        const double* y, double rho, double eta, double temperature,
        double* dydt, double* jacobian)
    {
        using namespace timmes_aprox21_detail;
        std::array<double, nrat> rate{};
        fill_screened_rates<double, timmes::RateValueAccessor>(
            y, rho, eta, temperature, temperature, rate);
        const double uncapped_he3ag = rate[irhe3ag];
        const double uncapped_npg = rate[irnpg];
        const double uncapped_iropg = rate[iropg];
        timmes::form_extended_equilibrium<true, RateIds, double>(
            rate, y, ihe4, ih1, ineut, iprot, temperature);
        rhs_aprox21(y, rate.data(), dydt);
        jacobian_aprox21_molar_fixed_rates(
            y, rate.data(), jacobian);

        constexpr int closure_columns[4]{ihe4, ih1, ineut, iprot};
        using AD = timmes::Dual<1>;
        for (int active = 0; active < 4; ++active) {
            const int column = closure_columns[active];
            AD y_ad[NUM_SPECIES];
            std::array<AD, nrat> rate_ad{};
            for (int i = 0; i < NUM_SPECIES; ++i) {
                y_ad[i] = i == column
                        ? AD::variable(y[i], 0) : AD(y[i]);
            }
            // Auxiliary equilibrium entries are reset by the closure, so its
            // final values can be reused. Restore the three pre-cap rates to
            // preserve scalar_min's original branch and derivative exactly.
            for (int i = 0; i < nrat; ++i) rate_ad[i] = AD(rate[i]);
            rate_ad[irhe3ag] = AD(uncapped_he3ag);
            rate_ad[irnpg] = AD(uncapped_npg);
            rate_ad[iropg] = AD(uncapped_iropg);
            timmes::form_extended_equilibrium<true, RateIds, AD>(
                rate_ad, y_ad, ihe4, ih1, ineut, iprot, temperature);
            add_aprox21_rate_derivative_column(
                y, rate.data(), rate_ad.data(), column, jacobian);
        }
    }
#undef TIMMES_NETWORK_JAC_NOINLINE

    template <typename Scalar>
    TIMMES_HD static inline void molar_rhs(
        const Scalar* y, double rho, double eta, double temperature, Scalar* dydt)
    {
        molar_rhs_impl<Scalar, timmes::RateValueAccessor>(
            y, rho, eta, temperature, temperature, dydt);
    }
};
