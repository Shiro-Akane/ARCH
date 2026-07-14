#pragma once

#include <array>

#include "../timmes_common/AproxRateAssembly.h"
#include "../timmes_common/TimmesNetworkSupport.h"
#include "TimmesRateLibrary.h"

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
    inline static constexpr auto MION = timmes::isotope_masses(AION, ZION, BION);

    template <typename Scalar>
    static inline void molar_rhs(const Scalar* y, double rho, double temperature, Scalar* dydt)
    {
        using namespace timmes_aprox21_detail;
        std::array<Scalar, nrat> rate{};
        if (temperature >= 1.0e6) {
            const timmes::TfactorsData tf = timmes::compute_tfactors(temperature);
            timmes::fill_heavy_rates<RateIds, timmes::Aprox21RateLibrary>(
                rate, temperature, rho, tf);
            timmes::fill_extended_rates<RateIds, timmes::Aprox21RateLibrary>(
                rate, temperature, rho, tf);
            timmes::fill_aprox21_extra_rates<RateIds, timmes::Aprox21RateLibrary>(
                rate, temperature, rho, tf);

            Scalar abar, zbar, z2bar, ye;
            timmes::composition_moments<Scalar, NUM_SPECIES>(
                y, ZION.data(), abar, zbar, z2bar, ye);
            timmes::screen_heavy_rates<RateIds>(
                rate, temperature, rho, zbar, abar, z2bar);
            timmes::screen_extended_rates<RateIds>(
                rate, temperature, rho, zbar, abar, z2bar);
            timmes::screen_aprox21_extra_rates<RateIds>(
                rate, temperature, rho, zbar, abar, z2bar);
            timmes::form_extended_equilibrium<true, RateIds, Scalar>(
                rate, y, ihe4, ih1, ineut, iprot, temperature);

            // irpen, irnep and irn56ec intentionally remain zero.  The
            // uploaded weak_aprox21 path requires eta_e from the Helmholtz
            // EOS, which is not part of ARCH's network interface.  This is
            // the explicit weak-rate stub allowed by implementation_plan.md.
        }
        rhs_aprox21(y, rate.data(), dydt);
    }
};
