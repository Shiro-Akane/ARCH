// C++ translation of Frank Timmes's screen5 Coulomb-screening routine.
// Upstream index: https://cococubed.com/code_pages/burn.shtml.
#pragma once

#include <algorithm>
#include <cmath>

#include "Dual.h"

namespace timmes {

// Timmes screen5, public_aprox21.f90:7602.  Scalar can carry either
// composition derivatives or the analytic temperature derivative.  Keeping
// temperature in the same scalar path restores the original screening
// contribution to drate/dT instead of recovering it with a finite difference.
template <typename Scalar>
TIMMES_HD inline Scalar screen5(const Scalar& temp, double den,
                      const Scalar& zbar, const Scalar& abar, const Scalar& z2bar,
                      double z1, double a1, double z2, double a2)
{
    constexpr double x13 = 1.0 / 3.0;
    constexpr double x14 = 1.0 / 4.0;
    constexpr double x53 = 5.0 / 3.0;
    constexpr double x532 = 5.0 / 32.0;
    constexpr double x512 = 5.0 / 12.0;
    constexpr double fact = 1.25992104989487;
    constexpr double co2 = x13 * 4.248719e3;
    constexpr double gamefx = 0.1;
    constexpr double gamefs = 0.4;
    constexpr double dgamma = 1.0 / (gamefs - gamefx);

    const double zs13 = std::pow(z1 + z2, x13);
    const double zs13inv = 1.0 / zs13;
    const double zhat = std::pow(z1 + z2, x53) - std::pow(z1, x53) - std::pow(z2, x53);
    const double zhat2 = std::pow(z1 + z2, x512) - std::pow(z1, x512) - std::pow(z2, x512);
    const double lzav = x53 * std::log(z1 * z2 / (z1 + z2));
    const double aznut = std::pow(z1 * z1 * z2 * z2 * a1 * a2 / (a1 + a2), x13);

    const Scalar ytot = 1.0 / abar;
    const Scalar rr_density = den * ytot;
    const Scalar tempi = 1.0 / temp;
    const Scalar pp = sqrt_value(rr_density * tempi * (z2bar + zbar));
    const Scalar qlam0z = 1.88e8 * tempi * pp;
    const Scalar taufac = co2 * pow_value(tempi, x13);
    const Scalar xni = pow_value(rr_density * zbar, x13);
    Scalar gamp = 2.27493e5 * tempi * xni;

    const double bb = z1 * z2;
    const double qq = fact * bb * zs13inv;
    Scalar gamef = qq * gamp;
    const Scalar tau12 = taufac * aznut;
    Scalar alph12 = gamef / tau12;

    if (value_of(alph12) > 1.6) {
        alph12 = Scalar(1.6);
        gamef = 1.6 * tau12;
        gamp = gamef * (zs13 / (fact * bb));
    }

    const Scalar h12w = bb * qlam0z;
    Scalar h12 = h12w;

    if (value_of(gamef) > gamefx) {
        const Scalar gamp14 = pow_value(gamp, x14);
        const Scalar cc = 0.896434 * gamp * zhat
                        - 3.44740 * gamp14 * zhat2
                        - 0.5551 * (log_value(gamp) + lzav)
                        - 2.996;
        const Scalar a3 = alph12 * alph12 * alph12;
        const Scalar q28 = 0.014 + 0.0128 * alph12;
        const Scalar r28 = x532 - alph12 * q28;
        const Scalar s28 = tau12 * r28;
        const Scalar t31 = -0.0098 + 0.0048 * alph12;
        const Scalar u31 = 0.0055 + alph12 * t31;
        const Scalar v31 = gamef * alph12 * u31;
        h12 = cc - a3 * (s28 + v31);

        Scalar xlgfac = 1.0 - 0.0562 * a3;
        if (value_of(xlgfac) < 0.77) xlgfac = Scalar(0.77);
        h12 = log_value(xlgfac) + h12;

        if (value_of(gamef) <= gamefs) {
            const Scalar weak_weight = dgamma * (gamefs - gamef);
            const Scalar strong_weight = dgamma * (gamef - gamefx);
            h12 = h12w * weak_weight + h12 * strong_weight;
        }
    }

    h12 = clamp_by_value(h12, 0.0, 30.0);
    return exp_value(h12);
}

template <typename Scalar, std::size_t N>
TIMMES_HD inline void composition_moments(const Scalar* y, const double* z,
                                Scalar& abar, Scalar& zbar, Scalar& z2bar, Scalar& ye)
{
    Scalar ytot = 0.0;
    Scalar zsum = 0.0;
    Scalar z2sum = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
        ytot += y[i];
        zsum += z[i] * y[i];
        z2sum += z[i] * z[i] * y[i];
    }
    abar = 1.0 / ytot;
    zbar = zsum * abar;
    z2bar = z2sum * abar;
    ye = zsum;
}

} // namespace timmes
