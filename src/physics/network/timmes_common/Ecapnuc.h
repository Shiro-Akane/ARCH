#pragma once

#include <cmath>
#include <type_traits>
#include "TimmesNetworkSupport.h"

namespace timmes {

template <typename Scalar>
TIMMES_HD inline void ecapnuc(double etakep, const Scalar& temp,
                              Scalar& rpen, Scalar& rnep,
                              Scalar& spenc, Scalar& snepc)
{
    constexpr double qn1    = -2.0716446e-06;
    constexpr double ftinv  = 1.0 / 1083.9269;
    constexpr double twoln  = 0.6931472;
    constexpr double cmk5   = 1.3635675e-49;
    constexpr double cmk6   = 2.2993864e-59;
    constexpr double bk     = 1.38062e-16;
    constexpr double qn2    = 2.0716446e-06;
    constexpr double c2me   = 8.1872665e-07;
    constexpr double pi     = 3.1415927;
    constexpr double pi2    = pi * pi;

    rpen   = 0.0;
    rnep   = 0.0;
    Scalar bktinv = 1.0 / (bk * temp);
    double qn     = qn1;

    Scalar etaef = etakep + c2me * bktinv;
    Scalar eta, t5, rie1, rie2, rjv1, rjv2;

    double eta_val = 0.0; // Define eta_val outside to be used at the end

    for (int iflag = 1; iflag <= 2; ++iflag) {
        Scalar etael;
        if (iflag == 1) {
            etael = qn2 * bktinv;
        } else {
            etael = c2me * bktinv;
            etaef = -etaef;
        }

        t5    = temp * temp * temp * temp * temp;
        const Scalar zetan = qn * bktinv;
        eta   = etaef - etael;

        if constexpr (std::is_same_v<Scalar, double>) {
            eta_val = eta;
        } else {
            eta_val = value_of(eta);
        }
        
        const Scalar exeta = (eta_val <= 6.8e2) ? exp_value(eta) : Scalar(0.0);
        const Scalar etael2 = etael * etael;
        const Scalar etael3 = etael2 * etael;
        const Scalar etael4 = etael3 * etael;
        const Scalar etael5 = etael4 * etael;
        const Scalar zetan2 = zetan * zetan;
        const Scalar f0 = (eta_val <= 6.8e2) ? log_value(Scalar(1.0) + exeta) : eta;

        const Scalar f1l = exeta;
        const Scalar f2l = 2.0 * f1l;
        const Scalar f3l = 6.0 * f1l;
        const Scalar f4l = 24.0 * f1l;
        const Scalar f5l = 120.0 * f1l;

        Scalar f1g = 0.0;
        Scalar f2g = 0.0;
        Scalar f3g = 0.0;
        Scalar f4g = 0.0;
        Scalar f5g = 0.0;
        if (eta_val > 0.0) {
            const Scalar exmeta = exp_value(-eta);
            const Scalar eta2   = eta * eta;
            const Scalar eta3   = eta2 * eta;
            const Scalar eta4   = eta3 * eta;
            f1g = 0.5 * eta2 + 2.0 - exmeta;
            f2g = eta3 * (1.0/3.0) + 4.0 * eta + 2.0 * exmeta;
            f3g = 0.25 * eta4 + 0.5 * pi2 * eta2 + 12.0 - 6.0 * exmeta;
            f4g = 0.2 * eta4 * eta + 2.0 * pi2 * (1.0/3.0) * eta3 + 48.0 * eta + 24.0 * exmeta;
            f5g = eta4 * eta2 * (1.0/6.0) + 5.0 * (1.0/6.0) * pi2 * eta4
                + 7.0 * (1.0/6.0) * pi2 * eta2 + 240.0 - 120.0 * exmeta;
        }

        const Scalar fac3 = 2.0 * zetan + 4.0 * etael;
        const Scalar fac2 = 6.0 * etael2 + 6.0 * etael * zetan + zetan2;
        const Scalar fac1 = 4.0 * etael3 + 6.0 * etael2 * zetan + 2.0 * etael * zetan2;
        const Scalar fac0 = etael4 + 2.0 * zetan * etael3 + etael2 * zetan2;

        rie1 = f4l + fac3 * f3l + fac2 * f2l + fac1 * f1l + fac0 * f0;
        rie2 = f4g + fac3 * f3g + fac2 * f2g + fac1 * f1g + fac0 * f0;

        const Scalar facv4 = 5.0 * etael + 3.0 * zetan;
        const Scalar facv3 = 10.0 * etael2 + 12.0 * etael * zetan + 3.0 * zetan2;
        const Scalar facv2 = 10.0 * etael3 + 18.0 * etael2 * zetan + 9.0 * etael * zetan2 + zetan2 * zetan;
        const Scalar facv1 = 5.0 * etael4 + 12.0 * etael3 * zetan + 9.0 * etael2 * zetan2 + 2.0 * etael * zetan2 * zetan;
        const Scalar facv0 = etael5 + 3.0 * etael4 * zetan + 3.0 * etael3 * zetan2 + etael2 * zetan2 * zetan;
        
        rjv1  = f5l + facv4 * f4l + facv3 * f3l + facv2 * f2l + facv1 * f1l + facv0 * f0;
        rjv2  = f5g + facv4 * f4g + facv3 * f3g + facv2 * f2g + facv1 * f1g + facv0 * f0;

        if (iflag < 2) {
            if (eta_val <= 0.0) {
                rpen  = twoln * cmk5 * t5 * rie1 * ftinv;
                spenc = twoln * cmk6 * t5 * temp * rjv1 * ftinv * c2me;
            } else {
                rpen = twoln * cmk5 * t5 * rie2 * ftinv;
                spenc = twoln * cmk6 * t5 * temp * rjv2 * ftinv * c2me;
            }
            qn = qn2;
        }
    }

    if (eta_val <= 0.0) {
        rnep  = twoln * cmk5 * t5 * rie1 * ftinv;
        snepc = twoln * cmk6 * t5 * temp * rjv1 * ftinv * c2me;
    } else {
        rnep  = twoln * cmk5 * t5 * rie2 * ftinv;
        snepc = twoln * cmk6 * t5 * temp * rjv2 * ftinv * c2me;
    }
}

} // namespace timmes
