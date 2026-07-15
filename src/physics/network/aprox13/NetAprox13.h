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
#include "TimmesJacobian.inc"

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
    inline static constexpr auto BINDING_E = BION;
    inline static constexpr std::array<double, NUM_SPECIES> SPIN{
        1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0,
        1.0, 1.0, 1.0, 1.0, 1.0, 1.0
    };
    inline static constexpr auto MION = timmes::isotope_masses(AION, ZION, BION);
    inline static constexpr auto ENERGY_WEIGHTS = MION;
    static constexpr double ENERGY_CONVERSION = timmes::constants::enuc_conv2;
    static constexpr double NSE_ENERGY_CONVERSION = timmes::constants::enuc_conv;

    // Device-safe scalar accessors.  Namespace-scope std::array storage is
    // host-only under NVCC when the index is dynamic, while these aprox13
    // alpha-chain values have an exact closed form.
    TIMMES_HD static constexpr double aion(int i)
    {
        return i == 0 ? 4.0 : 4.0 * (i + 2);
    }
    TIMMES_HD static constexpr double zion(int i)
    {
        return i == 0 ? 2.0 : 2.0 * (i + 2);
    }

    template <typename Scalar, typename RateAccessor>
    TIMMES_HD static inline void fill_screened_rates(const Scalar* y, double rho,
                                           double temperature_value,
                                           const Scalar& temperature,
                                           std::array<Scalar,
                                               timmes_aprox13_detail::nrat>& rate)
    {
        using namespace timmes_aprox13_detail;
        if (temperature_value >= 1.0e6) {
            const timmes::TfactorsData tf = timmes::compute_tfactors(temperature_value);
            timmes::fill_heavy_rates<RateIds, timmes::Aprox13RateLibrary, RateAccessor>(
                rate, temperature_value, rho, tf);

            double zion_values[NUM_SPECIES];
            for (int i = 0; i < NUM_SPECIES; ++i) zion_values[i] = zion(i);
            Scalar abar, zbar, z2bar, ye;
            timmes::composition_moments<Scalar, NUM_SPECIES>(
                y, zion_values, abar, zbar, z2bar, ye);
            timmes::screen_heavy_rates<RateIds>(
                rate, temperature, rho, zbar, abar, z2bar);
            timmes::form_alpha_branch_ratios<true, RateIds, Scalar>(rate);
        }
    }

    template <typename Scalar, typename RateAccessor>
    TIMMES_HD static inline void molar_rhs_impl(const Scalar* y, double rho,
                                      double temperature_value,
                                      const Scalar& temperature, Scalar* dydt)
    {
        using namespace timmes_aprox13_detail;
        std::array<Scalar, nrat> rate{};
        fill_screened_rates<Scalar, RateAccessor>(
            y, rho, temperature_value, temperature, rate);
        rhs_aprox13(y, rate.data(), dydt);
    }

    template <typename Scalar>
    TIMMES_HD static inline void molar_rhs_frozen_screening(
        const Scalar* y, double rho, double temperature, Scalar* dydt)
    {
        using namespace timmes_aprox13_detail;
        std::array<double, NUM_SPECIES> y_value{};
        for (int i = 0; i < NUM_SPECIES; ++i) {
            y_value[i] = timmes::value_of(y[i]);
        }
        std::array<double, nrat> rate_value{};
        fill_screened_rates<double, timmes::RateValueAccessor>(
            y_value.data(), rho, temperature, temperature, rate_value);
        std::array<Scalar, nrat> rate{};
        for (int i = 0; i < nrat; ++i) rate[i] = Scalar(rate_value[i]);
        rhs_aprox13(y, rate.data(), dydt);
    }

    TIMMES_HD static inline void molar_rhs_jacobian_frozen_screening(
        const double* y, double rho, double temperature,
        double* dydt, double* jacobian)
    {
        using namespace timmes_aprox13_detail;
        std::array<double, nrat> rate{};
        fill_screened_rates<double, timmes::RateValueAccessor>(
            y, rho, temperature, temperature, rate);
        rhs_aprox13(y, rate.data(), dydt);
        jacobian_aprox13_molar(y, rate.data(), jacobian);
    }

    template <typename Scalar>
    TIMMES_HD static inline void molar_rhs(const Scalar* y, double rho, double temperature,
                                 Scalar* dydt)
    {
        molar_rhs_impl<Scalar, timmes::RateValueAccessor>(
            y, rho, temperature, Scalar(temperature), dydt);
    }
};
