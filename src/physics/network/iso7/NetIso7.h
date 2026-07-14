#pragma once

#include <array>
#include <cmath>

#include "../timmes_common/AproxRateAssembly.h"
#include "../timmes_common/TimmesNetworkSupport.h"
#include "../timmes_common/TfactorsData.h"
#include "TimmesRateLibrary.h"

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
    inline static constexpr auto MION = timmes::isotope_masses(AION, ZION, BION);

    template <typename Scalar>
    static inline void molar_rhs(const Scalar* y, double rho, double temperature, Scalar* dydt)
    {
        using namespace timmes_iso7_detail;
        std::array<Scalar, nrat> rate{};
        if (temperature < 1.0e6) {
            rhs_iso7(y, rate.data(), dydt);
            return;
        }

        const timmes::TfactorsData tf = timmes::compute_tfactors(temperature);
        using RateLibrary = timmes::Iso7RateLibrary;
        auto pair = RateLibrary::rate_c12ag(temperature, rho, tf);
        rate[ircag] = pair.forward; rate[iroga] = pair.reverse;
        pair = RateLibrary::rate_tripalf(temperature, rho, tf);
        rate[ir3a] = pair.forward; rate[irg3a] = pair.reverse;
        rate[ir1212] = RateLibrary::rate_c12c12(temperature, rho, tf).forward;
        rate[ir1216] = RateLibrary::rate_c12o16(temperature, rho, tf).forward;
        rate[ir1616] = RateLibrary::rate_o16o16(temperature, rho, tf).forward;
        pair = RateLibrary::rate_o16ag(temperature, rho, tf);
        rate[iroag] = pair.forward; rate[irnega] = pair.reverse;
        pair = RateLibrary::rate_ne20ag(temperature, rho, tf);
        rate[irneag] = pair.forward; rate[irmgga] = pair.reverse;
        pair = RateLibrary::rate_mg24ag(temperature, rho, tf);
        rate[irmgag] = pair.forward; rate[irsiga] = pair.reverse;
        pair = RateLibrary::rate_ca40ag(temperature, rho, tf);
        rate[ircaag] = pair.forward; rate[irtiga] = pair.reverse;

        Scalar abar, zbar, z2bar, ye;
        timmes::composition_moments<Scalar, NUM_SPECIES>(
            y, ZION.data(), abar, zbar, z2bar, ye);
        auto screen = [&](double z1, double a1, double z2, double a2) {
            return timmes::screen5(temperature, rho, zbar, abar, z2bar, z1, a1, z2, a2);
        };

        rate[ir3a] *= screen(2.0, 4.0, 2.0, 4.0) * screen(2.0, 4.0, 4.0, 8.0);
        rate[ircag] *= screen(6.0, 12.0, 2.0, 4.0);
        rate[ir1212] *= screen(6.0, 12.0, 6.0, 12.0);
        rate[ir1216] *= screen(6.0, 12.0, 8.0, 16.0);
        // Preserve public_iso7.f90:2018 exactly: that source writes the screened
        // O16+O16 rate into ir1216 and leaves ir1616 raw.
        rate[ir1216] = RateLibrary::rate_o16o16(temperature, rho, tf).forward
                     * screen(8.0, 16.0, 8.0, 16.0);
        rate[iroag] *= screen(8.0, 16.0, 2.0, 4.0);
        rate[irneag] *= screen(10.0, 20.0, 2.0, 4.0);
        rate[irmgag] *= screen(12.0, 24.0, 2.0, 4.0);
        rate[ircaag] *= screen(20.0, 40.0, 2.0, 4.0);

        rate[irsi2ni] = Scalar(0.0);
        rate[irni2si] = Scalar(0.0);
        if (tf.t9 > 2.5 && timmes::value_of(y[ic12] + y[io16]) <= 4.0e-3) {
            const double t992 = tf.t972 * tf.t9;
            const double yeff_ca40 = std::exp(239.42 * tf.t9i - 74.741) / t992;
            const double yeff_ti44 = t992 * std::exp(-274.12 * tf.t9i + 74.914);
            const Scalar density_he3 = timmes::pow_value(rho * y[ihe4], 3.0);
            rate[irsi2ni] = yeff_ca40 * density_he3 * rate[ircaag] * y[isi28];
            if (timmes::value_of(density_he3) != 0.0) {
                rate[irni2si] = timmes::scalar_min(
                    Scalar(1.0e10), yeff_ti44 * rate[irtiga] / density_he3);
            }
        }

        rhs_iso7(y, rate.data(), dydt);
    }
};
