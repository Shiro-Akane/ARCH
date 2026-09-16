// C++ adaptation of Frank Timmes's public_iso7.f90 network.
// ARCH supplies the policy interface; the rate equations and nuclear data
// trace to https://cococubed.com/code_pages/burn.shtml.
#pragma once

#include <array>
#include <cmath>

#include "TimmesRateLibrary.h"

#include "../timmes_common/AproxRateAssembly.h"
#include "../timmes_common/TimmesNetworkSupport.h"
#include "../timmes_common/TfactorsData.h"

namespace timmes_iso7_detail {

enum Species : int { ihe4, ic12, io16, ine20, img24, isi28, ini56 };
enum Rates : int {
    ircag, iroga, ir3a, irg3a, ir1212, ir1216, ir1616, iroag, irnega,
    irneag, irmgga, irmgag, irsiga, ircaag, irtiga, irsi2ni, irni2si,
    nrat
};

inline constexpr double sixth = 1.0 / 6.0;

#include "TimmesRhs.inc"

} // namespace timmes_iso7_detail

struct NetIso7 : timmes::TimmesNetworkSupport<NetIso7> {
    static constexpr int NUM_SPECIES = 7;
    static constexpr int ODE_NEQ = NUM_SPECIES + 1;
    static constexpr const char* NETWORK_NAME = "iso7";

    inline static constexpr std::array<const char*, NUM_SPECIES> SPECIES_NAMES{
        "he4", "c12", "o16", "ne20", "mg24", "si28", "ni56"
    };
    inline static constexpr std::array<double, NUM_SPECIES> AION{
        4.0, 12.0, 16.0, 20.0, 24.0, 28.0, 56.0
    };
    inline static constexpr std::array<double, NUM_SPECIES> ZION{
        2.0, 6.0, 8.0, 10.0, 12.0, 14.0, 28.0
    };
    inline static constexpr std::array<double, NUM_SPECIES> BION{
        28.29603, 92.16294, 127.62093, 160.64788, 198.25790, 236.53790, 484.00300
    };
    inline static constexpr auto BINDING_E = BION;
    inline static constexpr std::array<double, NUM_SPECIES> SPIN{
        1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0
    };
    inline static constexpr auto MION = timmes::isotope_masses(AION, ZION, BION);
    inline static constexpr auto ENERGY_WEIGHTS = BION;
    static constexpr double ENERGY_CONVERSION = timmes::constants::enuc_conv;
    static constexpr double NSE_ENERGY_CONVERSION = timmes::constants::enuc_conv;

    // NVCC cannot dynamically index the host-side std::array objects above
    // from device code.  Keep the source-of-truth arrays for host code and
    // expose their exact iso7 values through device-safe accessors.
    TIMMES_HD static constexpr double aion(int i)
    {
        return i == 0 ? 4.0 : (i < 6 ? 4.0 * (i + 2) : 56.0);
    }
    TIMMES_HD static constexpr double zion(int i)
    {
        return 0.5 * aion(i);
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
                                           double temperature_value,
                                           const Scalar& temperature,
                                           std::array<Scalar,
                                               timmes_iso7_detail::nrat>& rate)
    {
        using namespace timmes_iso7_detail;
        if (temperature_value < 1.0e6) return;

        const timmes::TfactorsData tf = timmes::compute_tfactors(temperature_value);
        using RateLibrary = timmes::Iso7RateLibrary;
        auto pair = RateLibrary::rate_c12ag(temperature_value, rho, tf);
        rate[ircag] = RateAccessor::forward(pair);
        rate[iroga] = RateAccessor::reverse(pair);
        pair = RateLibrary::rate_tripalf(temperature_value, rho, tf);
        rate[ir3a] = RateAccessor::forward(pair);
        rate[irg3a] = RateAccessor::reverse(pair);
        rate[ir1212] = RateAccessor::forward(
            RateLibrary::rate_c12c12(temperature_value, rho, tf));
        rate[ir1216] = RateAccessor::forward(
            RateLibrary::rate_c12o16(temperature_value, rho, tf));
        rate[ir1616] = RateAccessor::forward(
            RateLibrary::rate_o16o16(temperature_value, rho, tf));
        pair = RateLibrary::rate_o16ag(temperature_value, rho, tf);
        rate[iroag] = RateAccessor::forward(pair);
        rate[irnega] = RateAccessor::reverse(pair);
        pair = RateLibrary::rate_ne20ag(temperature_value, rho, tf);
        rate[irneag] = RateAccessor::forward(pair);
        rate[irmgga] = RateAccessor::reverse(pair);
        pair = RateLibrary::rate_mg24ag(temperature_value, rho, tf);
        rate[irmgag] = RateAccessor::forward(pair);
        rate[irsiga] = RateAccessor::reverse(pair);
        pair = RateLibrary::rate_ca40ag(temperature_value, rho, tf);
        rate[ircaag] = RateAccessor::forward(pair);
        rate[irtiga] = RateAccessor::reverse(pair);

        Scalar abar, zbar, z2bar, ye;
        double zion_values[NUM_SPECIES];
        for (int i = 0; i < NUM_SPECIES; ++i) zion_values[i] = zion(i);
        timmes::composition_moments<Scalar, NUM_SPECIES>(
            y, zion_values, abar, zbar, z2bar, ye);
        auto screen = [&](double z1, double a1, double z2, double a2) {
            return timmes::screen5(temperature, rho, zbar, abar, z2bar, z1, a1, z2, a2);
        };

        rate[ir3a] *= screen(2.0, 4.0, 2.0, 4.0) * screen(2.0, 4.0, 4.0, 8.0);
        rate[ircag] *= screen(6.0, 12.0, 2.0, 4.0);
        rate[ir1212] *= screen(6.0, 12.0, 6.0, 12.0);
        rate[ir1216] *= screen(6.0, 12.0, 8.0, 16.0);
        // Preserve public_iso7.f90:2018-2020 exactly: it writes the screened
        // O16+O16 value into ir1216 and leaves ir1616 raw, while the screening
        // contribution to d(ir1216)/dT still multiplies raw C12+O16.
        const Scalar raw_o16o16 = RateAccessor::forward(
            RateLibrary::rate_o16o16(temperature_value, rho, tf));
        const Scalar raw_c12o16 = RateAccessor::forward(
            RateLibrary::rate_c12o16(temperature_value, rho, tf));
        const Scalar screen_o16o16 = screen(8.0, 16.0, 8.0, 16.0);
        if constexpr (RateAccessor::tracks_temperature) {
            Scalar screened(timmes::value_of(raw_o16o16)
                            * timmes::value_of(screen_o16o16));
            screened.deriv[0] = raw_o16o16.deriv[0]
                              * timmes::value_of(screen_o16o16)
                              + timmes::value_of(raw_c12o16)
                              * screen_o16o16.deriv[0];
            rate[ir1216] = screened;
        } else {
            rate[ir1216] = raw_o16o16 * screen_o16o16;
        }
        rate[iroag] *= screen(8.0, 16.0, 2.0, 4.0);
        rate[irneag] *= screen(10.0, 20.0, 2.0, 4.0);
        rate[irmgag] *= screen(12.0, 24.0, 2.0, 4.0);
        rate[ircaag] *= screen(20.0, 40.0, 2.0, 4.0);
    }

    template <typename Scalar, typename RateAccessor>
    TIMMES_HD static inline void apply_equilibrium_rates(
        const Scalar* y, double rho, double temperature_value,
        const Scalar& temperature,
        std::array<Scalar, timmes_iso7_detail::nrat>& rate)
    {
        using namespace timmes_iso7_detail;
        rate[irsi2ni] = Scalar(0.0);
        rate[irni2si] = Scalar(0.0);
        const timmes::TfactorsData tf = timmes::compute_tfactors(temperature_value);
        if (tf.t9 > 2.5 && timmes::value_of(y[ic12] + y[io16]) <= 4.0e-3) {
            Scalar yeff_ca40;
            Scalar yeff_ti44;
            if constexpr (RateAccessor::tracks_temperature) {
                const Scalar t9 = temperature * 1.0e-9;
                const Scalar t992 = timmes::pow_value(t9, 4.5);
                yeff_ca40 = timmes::exp_value(239.42 / t9 - 74.741) / t992;
                yeff_ti44 = t992 * timmes::exp_value(-274.12 / t9 + 74.914);
            } else {
                const double t992 = tf.t972 * tf.t9;
                yeff_ca40 = std::exp(239.42 * tf.t9i - 74.741) / t992;
                yeff_ti44 = t992 * std::exp(-274.12 * tf.t9i + 74.914);
            }
            const Scalar density_he3 = timmes::pow_value(rho * y[ihe4], 3.0);
            rate[irsi2ni] = yeff_ca40 * density_he3 * rate[ircaag] * y[isi28];
            if (timmes::value_of(density_he3) != 0.0) {
                rate[irni2si] = timmes::scalar_min(
                    Scalar(1.0e10), yeff_ti44 * rate[irtiga] / density_he3);
            }
        }
    }

    template <typename Scalar, typename RateAccessor>
    TIMMES_HD static inline void molar_rhs_impl(const Scalar* y, double rho, double eta,
                                      double temperature_value,
                                      const Scalar& temperature, Scalar* dydt)
    {
        using namespace timmes_iso7_detail;
        std::array<Scalar, nrat> rate{};
        fill_screened_rates<Scalar, RateAccessor>(
            y, rho, temperature_value, temperature, rate);
        apply_equilibrium_rates<Scalar, RateAccessor>(
            y, rho, temperature_value, temperature, rate);
        rhs_iso7(y, rate.data(), dydt);
    }

    template <typename Scalar>
    TIMMES_HD static inline void molar_rhs_frozen_screening(
        const Scalar* y, double rho, double eta, double temperature, Scalar* dydt)
    {
        using namespace timmes_iso7_detail;
        std::array<double, NUM_SPECIES> y_value{};
        for (int i = 0; i < NUM_SPECIES; ++i) {
            y_value[i] = timmes::value_of(y[i]);
        }
        std::array<double, nrat> rate_value{};
        fill_screened_rates<double, timmes::RateValueAccessor>(
            y_value.data(), rho, temperature, temperature, rate_value);
        std::array<Scalar, nrat> rate{};
        for (int i = 0; i < nrat; ++i) rate[i] = Scalar(rate_value[i]);
        apply_equilibrium_rates<Scalar, timmes::RateValueAccessor>(
            y, rho, temperature, Scalar(temperature), rate);
        rhs_iso7(y, rate.data(), dydt);
    }

    template <typename Scalar>
    TIMMES_HD static inline void molar_rhs(
        const Scalar* y, double rho, double eta, double temperature, Scalar* dydt)
    {
        molar_rhs_impl<Scalar, timmes::RateValueAccessor>(
            y, rho, eta, temperature, Scalar(temperature), dydt);
    }
};
