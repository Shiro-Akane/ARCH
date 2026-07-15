#pragma once

#include <array>

#include "../timmes_common/AproxRateAssembly.h"
#include "../timmes_common/TimmesNetworkSupport.h"
#include "TimmesRateLibrary.h"

namespace timmes_aprox19_detail {

enum Species : int {
    ih1, ihe3, ihe4, ic12, in14, io16, ine20, img24, isi28, is32, iar36,
    ica40, iti44, icr48, ife52, ife54, ini56, ineut, iprot
};

#define TIMMES_APROX19_RATES(X) \
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
    X(irhe3ag) X(ircpg) X(irnpg) X(ifa) X(ifg) X(iropg) X(irnag) X(irr1) \
    X(irs1) X(irt1) X(iru1) X(irv1) X(irw1) X(irx1) X(ir1f54) X(ir2f54) \
    X(ir3f54) X(ir4f54) X(ir5f54) X(ir6f54) X(ir7f54) X(ir8f54) \
    X(iralf1) X(iralf2)

enum Rates : int {
#define X(name) name,
    TIMMES_APROX19_RATES(X)
#undef X
    nrat
};

struct RateIds {
#define X(name) static constexpr int name = timmes_aprox19_detail::name;
    TIMMES_APROX19_RATES(X)
#undef X
};

inline constexpr double sixth = 1.0 / 6.0;
inline constexpr double c54 = 56.0 / 54.0;

#include "TimmesRhs.inc"

#undef TIMMES_APROX19_RATES

} // namespace timmes_aprox19_detail

struct NetAprox19 : timmes::TimmesNetworkSupport<NetAprox19> {
    static constexpr int NUM_SPECIES = 19;
    static constexpr int ODE_NEQ = NUM_SPECIES + 1;
    static constexpr const char* NETWORK_NAME = "aprox19";

    inline static constexpr std::array<const char*, NUM_SPECIES> SPECIES_NAMES{
        "h1", "he3", "he4", "c12", "n14", "o16", "ne20", "mg24",
        "si28", "s32", "ar36", "ca40", "ti44", "cr48", "fe52", "fe54",
        "ni56", "neut", "prot"
    };
    inline static constexpr std::array<double, NUM_SPECIES> AION{
        1.0, 3.0, 4.0, 12.0, 14.0, 16.0, 20.0, 24.0, 28.0, 32.0,
        36.0, 40.0, 44.0, 48.0, 52.0, 54.0, 56.0, 1.0, 1.0
    };
    inline static constexpr std::array<double, NUM_SPECIES> ZION{
        1.0, 2.0, 2.0, 6.0, 7.0, 8.0, 10.0, 12.0, 14.0, 16.0,
        18.0, 20.0, 22.0, 24.0, 26.0, 26.0, 28.0, 0.0, 1.0
    };
    inline static constexpr std::array<double, NUM_SPECIES> BION{
        0.0, 7.71819, 28.29603, 92.16294, 104.65998, 127.62093, 160.64788,
        198.25790, 236.53790, 271.78250, 306.72020, 342.05680, 375.47720,
        411.46900, 447.70800, 471.7696, 484.00300, 0.0, 0.0
    };
    inline static constexpr auto BINDING_E = BION;
    // h1 duplicates the free-proton quantum state in this approximate
    // network.  A zero NSE weight keeps the bookkeeping species out of the
    // statistical sum; equilibrium free protons are stored in "prot".
    inline static constexpr std::array<double, NUM_SPECIES> SPIN{
        0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0,
        1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 2.0, 2.0
    };
    inline static constexpr auto MION = timmes::isotope_masses(AION, ZION, BION);
    inline static constexpr auto ENERGY_WEIGHTS = MION;
    static constexpr double ENERGY_CONVERSION = timmes::constants::enuc_conv2;
    static constexpr double NSE_ENERGY_CONVERSION = timmes::constants::enuc_conv;

    template <typename Scalar, typename RateAccessor>
    static inline void fill_screened_rates(const Scalar* y, double rho,
                                           double temperature_value,
                                           const Scalar& temperature,
                                           std::array<Scalar,
                                               timmes_aprox19_detail::nrat>& rate)
    {
        using namespace timmes_aprox19_detail;
        if (temperature_value >= 1.0e6) {
            const timmes::TfactorsData tf = timmes::compute_tfactors(temperature_value);
            timmes::fill_heavy_rates<RateIds, timmes::Aprox19RateLibrary, RateAccessor>(
                rate, temperature_value, rho, tf);
            timmes::fill_extended_rates<RateIds, timmes::Aprox19RateLibrary, RateAccessor>(
                rate, temperature_value, rho, tf);

            Scalar abar, zbar, z2bar, ye;
            timmes::composition_moments<Scalar, NUM_SPECIES>(
                y, ZION.data(), abar, zbar, z2bar, ye);
            timmes::screen_heavy_rates<RateIds>(
                rate, temperature, rho, zbar, abar, z2bar);
            timmes::screen_extended_rates<RateIds>(
                rate, temperature, rho, zbar, abar, z2bar);

            // irpen, irnep and irn56ec intentionally remain zero.  The
            // uploaded weak_aprox19 path requires eta_e from the Helmholtz
            // EOS, which is not part of ARCH's network interface.  This is
            // the explicit weak-rate stub allowed by implementation_plan.md.
        }
    }

    template <typename Scalar, typename RateAccessor>
    static inline void molar_rhs_impl(const Scalar* y, double rho,
                                      double temperature_value,
                                      const Scalar& temperature, Scalar* dydt)
    {
        using namespace timmes_aprox19_detail;
        std::array<Scalar, nrat> rate{};
        fill_screened_rates<Scalar, RateAccessor>(
            y, rho, temperature_value, temperature, rate);
        timmes::form_extended_equilibrium<false, RateIds, Scalar>(
            rate, y, ihe4, ih1, ineut, iprot, temperature_value);
        rhs_aprox19(y, rate.data(), dydt);
    }

    template <typename Scalar>
    static inline void molar_rhs_frozen_screening(
        const Scalar* y, double rho, double temperature, Scalar* dydt)
    {
        using namespace timmes_aprox19_detail;
        std::array<double, NUM_SPECIES> y_value{};
        for (int i = 0; i < NUM_SPECIES; ++i) {
            y_value[i] = timmes::value_of(y[i]);
        }
        std::array<double, nrat> rate_value{};
        fill_screened_rates<double, timmes::RateValueAccessor>(
            y_value.data(), rho, temperature, temperature, rate_value);
        std::array<Scalar, nrat> rate{};
        for (int i = 0; i < nrat; ++i) rate[i] = Scalar(rate_value[i]);
        timmes::form_extended_equilibrium<false, RateIds, Scalar>(
            rate, y, ihe4, ih1, ineut, iprot, temperature);
        rhs_aprox19(y, rate.data(), dydt);
    }

    template <typename Scalar>
    static inline void molar_rhs(const Scalar* y, double rho, double temperature,
                                 Scalar* dydt)
    {
        molar_rhs_impl<Scalar, timmes::RateValueAccessor>(
            y, rho, temperature, Scalar(temperature), dydt);
    }
};
