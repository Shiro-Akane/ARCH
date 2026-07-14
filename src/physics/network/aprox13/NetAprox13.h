#pragma once

#include <array>

#include "../timmes_common/AproxRateAssembly.h"
#include "../timmes_common/TimmesNetworkSupport.h"
#include "TimmesRateLibrary.h"

namespace timmes_aprox13_detail {

enum Species : int {
    ihe4, ic12, io16, ine20, img24, isi28, is32, iar36, ica40, iti44,
    icr48, ife52, ini56
};

#define TIMMES_APROX13_RATES(X) \
    X(ir3a) X(irg3a) X(ircag) X(iroga) X(ir1212) X(ir1216) X(ir1616) \
    X(iroag) X(irnega) X(irneag) X(irmgga) X(irmgag) X(irsiga) \
    X(irmgap) X(iralpa) X(iralpg) X(irsigp) X(irsiag) X(irsga) \
    X(irsiap) X(irppa) X(irppg) X(irsgp) X(irsag) X(irarga) X(irsap) \
    X(irclpa) X(irclpg) X(irargp) X(irarag) X(ircaga) X(irarap) X(irkpa) \
    X(irkpg) X(ircagp) X(ircaag) X(irtiga) X(ircaap) X(irscpa) X(irscpg) \
    X(irtigp) X(irtiag) X(ircrga) X(irtiap) X(irvpa) X(irvpg) X(ircrgp) \
    X(ircrag) X(irfega) X(ircrap) X(irmnpa) X(irmnpg) X(irfegp) X(irfeag) \
    X(irniga) X(irfeap) X(ircopa) X(ircopg) X(irnigp) X(irr1) X(irs1) \
    X(irt1) X(iru1) X(irv1) X(irw1) X(irx1) X(iry1)

enum Rates : int {
#define X(name) name,
    TIMMES_APROX13_RATES(X)
#undef X
    nrat
};

struct RateIds {
#define X(name) static constexpr int name = timmes_aprox13_detail::name;
    TIMMES_APROX13_RATES(X)
#undef X
};

inline constexpr std::array<const char*, nrat> RATE_NAMES{
#define X(name) #name,
    TIMMES_APROX13_RATES(X)
#undef X
};

inline constexpr double sixth = 1.0 / 6.0;

#include "TimmesRhs.inc"

#undef TIMMES_APROX13_RATES

} // namespace timmes_aprox13_detail

struct NetAprox13 : timmes::TimmesNetworkSupport<NetAprox13> {
    static constexpr int NUM_SPECIES = 13;
    static constexpr int ODE_NEQ = NUM_SPECIES + 1;
    static constexpr const char* NETWORK_NAME = "aprox13";

    inline static constexpr std::array<const char*, NUM_SPECIES> SPECIES_NAMES{
        "he4", "c12", "o16", "ne20", "mg24", "si28", "s32", "ar36",
        "ca40", "ti44", "cr48", "fe52", "ni56"
    };
    inline static constexpr std::array<double, NUM_SPECIES> AION{
        4.0, 12.0, 16.0, 20.0, 24.0, 28.0, 32.0, 36.0, 40.0, 44.0, 48.0, 52.0, 56.0
    };
    inline static constexpr std::array<double, NUM_SPECIES> ZION{
        2.0, 6.0, 8.0, 10.0, 12.0, 14.0, 16.0, 18.0, 20.0, 22.0, 24.0, 26.0, 28.0
    };
    inline static constexpr std::array<double, NUM_SPECIES> BION{
        28.29603, 92.16294, 127.62093, 160.64788, 198.25790, 236.53790,
        271.78250, 306.72020, 342.05680, 375.47720, 411.46900, 447.70800, 484.00300
    };
    inline static constexpr auto MION = timmes::isotope_masses(AION, ZION, BION);

    template <typename Scalar>
    static inline void molar_rhs(const Scalar* y, double rho, double temperature, Scalar* dydt)
    {
        using namespace timmes_aprox13_detail;
        std::array<Scalar, nrat> rate{};
        if (temperature >= 1.0e6) {
            const timmes::TfactorsData tf = timmes::compute_tfactors(temperature);
            timmes::fill_heavy_rates<RateIds, timmes::Aprox13RateLibrary>(
                rate, temperature, rho, tf);

            Scalar abar, zbar, z2bar, ye;
            timmes::composition_moments<Scalar, NUM_SPECIES>(
                y, ZION.data(), abar, zbar, z2bar, ye);
            timmes::screen_heavy_rates<RateIds>(
                rate, temperature, rho, zbar, abar, z2bar);
            timmes::form_alpha_branch_ratios<true, RateIds, Scalar>(rate);
        }
        rhs_aprox13(y, rate.data(), dydt);
    }
};
