/**
 * @file diffusion_math.hpp
 * @brief Pure mathematical logic for stellar thermal conductivity.
 *        Decoupled from AMReX, Grid, and EOS frameworks.
 * *
 * * Workflow:
 * * 1. Intake pure thermodynamic states (T, rho, P_ele, n_ele, eta) and isotope arrays.
 * * 2. Calculate the average charge (zbar) and average atomic weight (abar).
 * * 3. Compute the opacities from different scattering mechanisms (e-i, e-e, radiative).
 * * 4. Combine the opacities using Matthiessen's rule.
 * * 5. Return the final stellar thermal conductivity.
 */
#pragma once

#include <vector>
#include <algorithm>
#include <cmath>

namespace ConductivityMath {

    // =========================================================
    // =================== Physics Constants ===================
    // =========================================================
    namespace Constants {
        constexpr double c_light = 29979245800.0;
        constexpr double sigma_SB = 5.670374419184432e-05;
        constexpr double a_rad = 4.0 * sigma_SB / c_light;
        constexpr double n_A = 6.02214076e+23;

        constexpr double third  = 1.0 / 3.0;
        constexpr double twoth  = 2.0 * third;
        constexpr double zbound = 0.1;
        constexpr double t7peek = 1.0e20;
        constexpr double k2c    = 4.0 / 3.0 * a_rad * c_light;
        constexpr double meff   = 1.194648642401440e-10;
        constexpr double weid   = 6.884326138694269e-5;
        constexpr double iec    = 1.754582332329132e16;
        constexpr double xec    = 4.309054377592449e-7;
        constexpr double rt3    = 1.7320508075688772;
        constexpr double con2   = 1.07726359439811217e-7;
        constexpr double t6_switch1 = 0.5;
        constexpr double t6_switch2 = 0.9;
        constexpr double PI = 3.14159265358979323846;
    }

    // =========================================================
    // =================== Core Computation ====================
    // =========================================================

    /**
     * @brief Computes the thermal conductivity.
     * 
     * @param T Temperature (K)
     * @param rho Density (g/cm^3)
     * @param pele Electron-positron pressure (erg/cm^3)
     * @param xne Electron-positron number density (1/cm^3)
     * @param eta Electron degeneracy parameter
     * @param xn Mass fractions of composition
     * @param zion Charge of each isotope
     * @param aion_inv Inverse atomic weight of each isotope
     * @return Computed thermal conductivity (erg/cm/K/sec)
     */
    inline double compute_stellar_conductivity(
        double T, double rho, 
        double pele, double xne, double eta,
        const double* xn, int NumSpec,
        const double* zion, 
        const double* aion_inv) 
    {
        using namespace Constants;
        
        // =========================================================
        // 4. Electron-Ion / Electron-Electron Opacities
        // =========================================================
        double opac      = 0.0;
        double opac_ei   = 0.0, opac_ee = 0.0;
        double orad      = 0.0;
        double ocond     = 0.0;
        double oiben1    = 0.0;
        double oiben2    = 0.0;
        double ochrs     = 0.0;
        double oh        = 0.0;
        double ov        = 0.0;
        // =========================================================
        // 1. Abar and Zbar Setup
        // =========================================================
        double abar = 0.0, zbar = 0.0;
        double ytot1     = 0.0;

        double w[6] = {0.0};

        for (int i = 0; i < NumSpec; i++) {
            int iz = std::min(3, std::max(1, static_cast<int>(zion[i]))) - 1;
            double ymass = xn[i] * aion_inv[i];
            w[iz] += xn[i];
            w[iz+3] += zion[i] * zion[i] * ymass;
            zbar += zion[i] * ymass;
            ytot1 += ymass;
        }
        abar = 1.0 / ytot1;
        zbar = zbar * abar;
        double t6 = T * 1.0e-6;

        double xh = w[0];
        double xhe = w[1];
        double xz = w[2];

        auto powi2 = [](double val) { return val * val; };
        auto powi3 = [](double val) { return val * val * val; };
        auto powi4 = [](double val) { return val * val * val * val; };

        if (xh < 1.0e-5) {
            double xmu = std::max(1.0e-99, w[3] + w[4] + w[5] - 1.0);
            double xkc = std::pow((2.019e-4 * rho / std::pow(t6, 1.7)), 2.425);
            double xkap = 1.0 + xkc * (1.0 + xkc/24.55);
            double xkb = 3.86 + 0.252 * std::sqrt(xmu) + 0.018 * xmu;
            double xka = 3.437 * (1.25 + 0.488 * std::sqrt(xmu) + 0.092 * xmu);
            double dbar = std::exp(-xka + xkb * std::log(t6));
            oiben1 = xkap * std::pow(rho/dbar, 0.67);
        }

        if ( !((xh >=  1.0e-5) && (t6 < t6_switch1)) &&
             !((xh < 1.0e-5) && (xz > zbound)) ) {
            double d0log;
            if (t6 > t6_switch1) {
                d0log = -(3.868 + 0.806 * xh) + 1.8 * std::log(t6);
            } else {
                d0log = -(3.868 + 0.806 * xh) + (3.42 - 0.52 * xh) * std::log(t6);
            }
            double xka1 = 2.809 * std::exp(-(1.74  - 0.755 * xh)
                                    * powi2(std::log10(t6) - 0.22 + 0.1375 * xh));
            double xkw = 4.05 * std::exp(-(0.306  - 0.04125 * xh)
                                    * powi2(std::log10(t6) - 0.18 + 0.1625 * xh));
            double xkaz = 50.0 * xz * xka1 * std::exp(-0.5206 * powi2((std::log(rho)-d0log)/xkw));
            double dbar2log = -(4.283 + 0.7196 * xh) + 3.86 * std::log(t6);
            double dbar1log = -5.296 + 4.833 * std::log(t6);
            dbar1log = std::min(dbar1log, dbar2log);
            oiben2 = std::pow(rho/std::exp(dbar1log), 0.67) * std::exp(xkaz);
        }

        if ((t6 < t6_switch2) && (xh >= 1.0e-5)) {
            double t4 = T * 1.0e-4;
            double t4r = std::sqrt(t4);
            double t44 = t4*t4*t4*t4;
            double t45 = t44 * t4;
            double t46 = t45 * t4;
            double ck1 = 2.0e6/t44 + 2.1 * t46;
            double ck3 = 4.0e-3/t44 + 2.0e-4/std::pow(rho, 0.25);
            double ck2 = 4.5 * t46 + 1.0/(t4 * ck3);
            double ck4 = 1.4e3 * t4 + t46;
            double ck5 = 1.0e6 + 0.1 * t46;
            double ck6 = 20.0 * t4 + 5.0 * t44 + t45;
            double xkcx = xh * (t4r/ck1 + 1.0/ck2);
            double xkcy = xhe * (1.0/ck4 + 1.5/ck5);
            double xkcz = xz * (t4r/ck6);
            ochrs = pele * (xkcx + xkcy + xkcz);
        }

        if (xh >= 1.0e-5) {
            if (t6 < t6_switch1) {
                orad = ochrs;
            } else if (t6 <= t6_switch2) {
                orad = 2.0 * (ochrs * (1.5 - t6) + oiben2 * (t6 - 1.0));
            } else {
                orad = oiben2;
            }
        } else {
            if (xz > zbound) {
                orad = oiben1;
            } else {
                orad = oiben1 * (xz/zbound) + oiben2 * ((zbound-xz)/zbound);
            }
        }

        double th = std::min(511.0, T * 8.617e-8);
        double fact = 1.0 + 2.75e-2 * th - 4.88e-5 * th * th;
        double facetax = 1.0e100;
        if (eta <= 500.0) {
            facetax = std::exp(0.522 * eta - 1.563);
        }
        // =========================================================
        // 6. Matthiessen's Rule Combination
        // =========================================================
        double opac_total = opac_ee + opac_ei + orad;
        double faceta  = 1.0 + facetax;
        double ocompt = 6.65205e-25 / (fact * faceta) * xne / rho;
        orad += ocompt;

        double tcut = con2 * std::sqrt(xne);
        if (T < tcut) {
            if (tcut > 200.0 * T) {
                orad = orad * 2.658e86;
            } else {
                double cutfac = std::exp(tcut/T - 1.0);
                orad = orad * cutfac;
            }
        }

        double xkf = t7peek * rho * powi4(T * 1.0e-7);
        orad = xkf * orad / (xkf + orad);

        double dlog10 = std::log10(rho);
        double drel = 2.4e-7 * zbar/abar * T * std::sqrt(T);
        // =========================================================
        // 2. Early Exit (Low T or Negative Zbar)
        // =========================================================
        if (T < 1.0e2 || zbar <= 0.0) return 0.0;
        if (T <= 1.0e5) {
            drel = drel * 15.0;
        }
        double drel10 = std::log10(drel);
        double drelim = drel10 + 1.0;

        if (dlog10 < drelim) {
            double zdel = xne / (n_A * t6 * std::sqrt(t6));
            double zdell10 = std::log10(zdel);
            double eta0 = std::exp(-1.20322 + twoth * std::log(zdel));
            double eta02 = eta0 * eta0;

            double thpl;
            if (zdell10 < 0.645) {
                thpl = -7.5668 + std::log(zdel * (1.0 + 0.024417 * zdel));
            } else {
                if (zdell10 < 2.5) {
                    thpl = -7.58110 + std::log(zdel * (1.0 + 0.02804 * zdel));
                    if (zdell10 >= 2.0) {
                        double thpla = thpl;
                        thpl = -11.0742 + std::log(zdel * zdel * (1.0 + 9.376 / eta02));
                        thpl = 2.0 * ((2.5 - zdell10) * thpla + (zdell10 - 2.0) * thpl);
                    }
                } else {
                    thpl = -11.0742 + std::log(zdel * zdel * (1.0 + 9.376 / eta02));
                }
            }

            double pefac;
            if (zdell10 < 2.0) {
                pefac = 1.0 + 0.021876 * zdel;
                if (zdell10 > 1.5) {
                    double pefacal = std::log(pefac);
                    double pefacl = std::log(0.4 * eta0 + 1.64496 / eta0);
                    double cfac1 = 2.0 - zdell10;
                    double cfac2 = zdell10 - 1.5;
                    pefac = std::exp(2.0 * (cfac1 * pefacal + cfac2 * pefacl));
                }
            } else {
                pefac = 0.4 * eta0 + 1.64496 / eta0;
            }

            double dnefac;
            if (zdel < 40.0) {
                dnefac = 1.0 + zdel * (3.4838e-4 * zdel - 2.8966e-2);
            } else {
                dnefac = 1.5 / eta0 * (1.0 - 0.8225 / eta02);
            }
            double wpar2 = 9.24735e-3 * zdel *
                (rho * n_A * (w[3] + w[4] + w[5]) / xne + dnefac) / (std::sqrt(t6) * pefac);
            double walf = 0.5 * std::log(wpar2);
            double walf10 = 0.5 * std::log10(wpar2);

            double thx;
            if (walf10 <= -3.0) {
                thx = std::exp(2.413 - 0.124 * walf);
            } else if (walf10 <= -1.0) {
                thx = std::exp(0.299 - walf * (0.745 + 0.0456 * walf));
            } else {
                thx = std::exp(0.426 - 0.558 * walf);
            }

            double thy;
            if (walf10 <= -3.0) {
                thy = std::exp(2.158 - 0.111 * walf);
            } else if (walf10 <= 0.0) {
                thy = std::exp(0.553 - walf * (0.55 + 0.0299 * walf));
            } else {
                thy = std::exp(0.553 - 0.6 * walf);
            }

            double thc;
            if (walf10 <= -2.5) {
                thc = std::exp(2.924 - 0.1 * walf);
            } else if (walf10 <= 0.5) {
                thc = std::exp(1.6740 - walf * (0.511 + 0.0338 * walf));
            } else {
                thc = std::exp(1.941 - 0.785 * walf);
            }

            oh = (xh * thx + xhe * thy + w[5] * third * thc) / (t6 * std::exp(thpl));
        }

        if (dlog10 > drel10) {
            double xmas = meff * std::cbrt(xne);
            double ymas = std::sqrt(1.0 + xmas * xmas);
            double wfac = weid * T / ymas * xne;
            double cint = 1.0;

            // =========================================================
            // 3. Mathematical Intermediates
            // =========================================================
            double con5 = iec / zbar;
            double vie = con5 * ymas * cint;
            double cie = wfac / vie;

            double tpe = xec * std::sqrt(xne / ymas);
            double yg = rt3 * tpe / T;
            double xrel = 1.009 * std::cbrt(zbar / abar * rho * 1.0e-6);
            double beta2 = xrel * xrel / (1.0 + xrel * xrel);
            double jy = (1.0 + 6.0 / (5.0 * xrel * xrel) + 2.0 / (5.0 * powi4(xrel)))
                * (powi3(yg) / (3.0 * powi3(1.0 + 0.07414 * yg))
                * std::log((2.81 - 0.810 * beta2 + yg) / yg)
                + std::pow(PI, 5.0 / 6.0) * powi4(yg / (13.91 + yg)));
            double vee = 0.511 * T * T * xmas / (ymas * ymas) * std::sqrt(xmas / ymas) * jy;
            double cee = wfac / vee;

            double ov1 = cie * cee / (cee + cie);
            ov = k2c / (ov1 * rho) * T * T * T;
        }

        if (dlog10 <= drel10) {
            ocond = oh;
        } else if (dlog10 > drel10 && dlog10 < drelim) {
            double x = rho;
            double x1 = std::pow(10.0, drel10);
            double x2 = std::pow(10.0, drelim);
            double alfa = (x - x2) / (x1 - x2);
            double beta = (x - x1) / (x2 - x1);
            ocond = alfa * oh + beta * ov;
        } else if (dlog10 >= drelim) {
            ocond = ov;
        }

        opac = orad * ocond / (ocond + orad);
        return k2c * T * T * T / (opac * rho);
    }
}
