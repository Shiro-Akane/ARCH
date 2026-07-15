#pragma once

#include <iostream>
#include <fstream>
#include <array>
#include <vector>
#include <cmath>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <iomanip>

#include "eos.h"
#include "eos_Utils.h"
#include "../../data/FluidState.h"
#include "../species/Species.h"

// Timmes Helmholtz EOS (Electron/Positron table + Analytic Ion/Rad)
class HelmEos : public EOSBase {
private:
    static constexpr int imax = 541;
    static constexpr int jmax = 201;

    static constexpr double dlo = -12.0;
    static constexpr double dhi = 15.0;
    static constexpr double dstp = 0.05;
    static constexpr double dstpi = 1.0 / dstp;

    static constexpr double tlo = 3.0;
    static constexpr double thi = 13.0;
    static constexpr double tstp = 0.05;
    static constexpr double tstpi = 1.0 / tstp;

    // Table data
    std::vector<double> f[9]; 

    const SpeciesManager* specs;

    // Quintic Hermite polynomials
    double psi0(double z) const { return z * z * z * (z * (-6.0 * z + 15.0) - 10.0) + 1.0; }
    double dpsi0(double z) const { return z * z * (z * (-30.0 * z + 60.0) - 30.0); }
    double ddpsi0(double z) const { return z * (z * (-120.0 * z + 180.0) - 60.0); }
    
    double psi1(double z) const { return z * (z * z * (z * (-3.0 * z + 8.0) - 6.0) + 1.0); }
    double dpsi1(double z) const { return z * z * (z * (-15.0 * z + 32.0) - 18.0) + 1.0; }
    double ddpsi1(double z) const { return z * (z * (-60.0 * z + 96.0) - 36.0); }
    
    double psi2(double z) const { return 0.5 * z * z * (z * (z * (-z + 3.0) - 3.0) + 1.0); }
    double dpsi2(double z) const { return 0.5 * z * (z * (z * (-5.0 * z + 12.0) - 9.0) + 2.0); }
    double ddpsi2(double z) const { return 1.0 + z * (-9.0 + z * (18.0 - 10.0 * z)); }

    // Physical Constants (matching helmholtz.f90)
    static constexpr double kerg = 1.380650424e-16;
    static constexpr double amu  = 1.66053878283e-24;
    static constexpr double avo  = 6.0221417930e23;
    static constexpr double clight = 2.99792458e10;
    static constexpr double ssol = 5.6704e-5;
    static constexpr double asol = 4.0 * ssol / clight;
    static constexpr double m_p  = 1.67262163783e-24; // proton mass

public:
    HelmEos(const std::string& table_path, const SpeciesManager* specs_) : specs(specs_) {
        std::cout << "[HelmEos] Loading 2D Helmholtz table from " << table_path << "..." << std::endl;
        std::ifstream file(table_path);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open helm_table.dat at " + table_path);
        }

        for (int k = 0; k < 9; ++k) f[k].resize(imax * jmax);

        // helm_table.dat has 4 blocks. The first block is the free energy f (108741 lines)
        // Each line has 9 values. Fortran outputs column-major: do j=1,jmax; do i=1,imax
        for (int j = 0; j < jmax; ++j) {
            for (int i = 0; i < imax; ++i) {
                int idx = j * imax + i;
                for (int k = 0; k < 9; ++k) {
                    file >> f[k][idx];
                }
            }
        }
        std::cout << "[HelmEos] 2D Helmholtz Electron/Positron table loaded successfully." << std::endl;
    }

    void interpolate_ele_pos(double rho, double T, double ye,
                             double& P_ele, double& E_ele,
                             double* cv_ele = nullptr) const {
        double din = rho * ye;
        double d_val = std::log10(din);
        double t_val = std::log10(T);

        // clamp
        d_val = std::max(dlo, std::min(d_val, dhi));
        t_val = std::max(tlo, std::min(t_val, thi));

        int i = static_cast<int>((d_val - dlo) * dstpi);
        int j = static_cast<int>((t_val - tlo) * tstpi);

        i = std::max(0, std::min(i, imax - 2));
        j = std::max(0, std::min(j, jmax - 2));

        double d_node = std::pow(10.0, dlo + i * dstp);
        double d_next = std::pow(10.0, dlo + (i + 1) * dstp);
        double dd = d_next - d_node;
        double ddi = 1.0 / dd;

        double t_node = std::pow(10.0, tlo + j * tstp);
        double t_next = std::pow(10.0, tlo + (j + 1) * tstp);
        double dth = t_next - t_node;
        double dti = 1.0 / dth;

        double xd = std::max( (din - d_node) * ddi, 0.0 );
        double xt = std::max( (T - t_node) * dti, 0.0 );

        double w0d = psi0(xd), w1d = psi1(xd) * dd, w2d = psi2(xd) * dd * dd;
        double w0md = psi0(1.0 - xd), w1md = -psi1(1.0 - xd) * dd, w2md = psi2(1.0 - xd) * dd * dd;

        double w0t = psi0(xt), w1t = psi1(xt) * dth, w2t = psi2(xt) * dth * dth;
        double w0mt = psi0(1.0 - xt), w1mt = -psi1(1.0 - xt) * dth, w2mt = psi2(1.0 - xt) * dth * dth;

        double fi[36];
        int idx00 = j * imax + i;
        int idx10 = j * imax + i + 1;
        int idx01 = (j + 1) * imax + i;
        int idx11 = (j + 1) * imax + i + 1;

        int offset[9] = {0, 12, 4, 16, 8, 20, 24, 28, 32};
        
        for (int k = 0; k < 9; ++k) {
            int off = offset[k];
            fi[off + 0] = f[k][idx00];
            fi[off + 1] = f[k][idx10];
            fi[off + 2] = f[k][idx01];
            fi[off + 3] = f[k][idx11];
        }

        double free_energy = 
             fi[0]*w0d*w0t   + fi[1]*w0md*w0t  + fi[2]*w0d*w0mt  + fi[3]*w0md*w0mt
           + fi[4]*w0d*w1t   + fi[5]*w0md*w1t  + fi[6]*w0d*w1mt  + fi[7]*w0md*w1mt
           + fi[8]*w0d*w2t   + fi[9]*w0md*w2t  + fi[10]*w0d*w2mt + fi[11]*w0md*w2mt
           + fi[12]*w1d*w0t  + fi[13]*w1md*w0t + fi[14]*w1d*w0mt + fi[15]*w1md*w0mt
           + fi[16]*w2d*w0t  + fi[17]*w2md*w0t + fi[18]*w2d*w0mt + fi[19]*w2md*w0mt
           + fi[20]*w1d*w1t  + fi[21]*w1md*w1t + fi[22]*w1d*w1mt + fi[23]*w1md*w1mt
           + fi[24]*w2d*w1t  + fi[25]*w2md*w1t + fi[26]*w2d*w1mt + fi[27]*w2md*w1mt
           + fi[28]*w1d*w2t  + fi[29]*w1md*w2t + fi[30]*w1d*w2mt + fi[31]*w1md*w2mt
           + fi[32]*w2d*w2t  + fi[33]*w2md*w2t + fi[34]*w2d*w2mt + fi[35]*w2md*w2mt;

        double d0d = dpsi0(xd)*ddi, d1d = dpsi1(xd), d2d = dpsi2(xd)*dd;
        double d0md = -dpsi0(1.0 - xd)*ddi, d1md = dpsi1(1.0 - xd), d2md = -dpsi2(1.0 - xd)*dd;

        double df_d = 
             fi[0]*d0d*w0t  + fi[1]*d0md*w0t  + fi[2]*d0d*w0mt  + fi[3]*d0md*w0mt
           + fi[4]*d0d*w1t  + fi[5]*d0md*w1t  + fi[6]*d0d*w1mt  + fi[7]*d0md*w1mt
           + fi[8]*d0d*w2t  + fi[9]*d0md*w2t  + fi[10]*d0d*w2mt + fi[11]*d0md*w2mt
           + fi[12]*d1d*w0t + fi[13]*d1md*w0t + fi[14]*d1d*w0mt + fi[15]*d1md*w0mt
           + fi[16]*d2d*w0t + fi[17]*d2md*w0t + fi[18]*d2d*w0mt + fi[19]*d2md*w0mt
           + fi[20]*d1d*w1t + fi[21]*d1md*w1t + fi[22]*d1d*w1mt + fi[23]*d1md*w1mt
           + fi[24]*d2d*w1t + fi[25]*d2md*w1t + fi[26]*d2d*w1mt + fi[27]*d2md*w1mt
           + fi[28]*d1d*w2t + fi[29]*d1md*w2t + fi[30]*d1d*w2mt + fi[31]*d1md*w2mt
           + fi[32]*d2d*w2t + fi[33]*d2md*w2t + fi[34]*d2d*w2mt + fi[35]*d2md*w2mt;

        double d0t = dpsi0(xt)*dti, d1t = dpsi1(xt), d2t = dpsi2(xt)*dth;
        double d0mt = -dpsi0(1.0 - xt)*dti, d1mt = dpsi1(1.0 - xt), d2mt = -dpsi2(1.0 - xt)*dth;

        double df_t = 
             fi[0]*w0d*d0t  + fi[1]*w0md*d0t  + fi[2]*w0d*d0mt  + fi[3]*w0md*d0mt
           + fi[4]*w0d*d1t  + fi[5]*w0md*d1t  + fi[6]*w0d*d1mt  + fi[7]*w0md*d1mt
           + fi[8]*w0d*d2t  + fi[9]*w0md*d2t  + fi[10]*w0d*d2mt + fi[11]*w0md*d2mt
           + fi[12]*w1d*d0t + fi[13]*w1md*d0t + fi[14]*w1d*d0mt + fi[15]*w1md*d0mt
           + fi[16]*w2d*d0t + fi[17]*w2md*d0t + fi[18]*w2d*d0mt + fi[19]*w2md*d0mt
           + fi[20]*w1d*d1t + fi[21]*w1md*d1t + fi[22]*w1d*d1mt + fi[23]*w1md*d1mt
           + fi[24]*w2d*d1t + fi[25]*w2md*d1t + fi[26]*w2d*d1mt + fi[27]*w2md*d1mt
           + fi[28]*w1d*d2t + fi[29]*w1md*d2t + fi[30]*w1d*d2mt + fi[31]*w1md*d2mt
           + fi[32]*w2d*d2t + fi[33]*w2md*d2t + fi[34]*w2d*d2mt + fi[35]*w2md*d2mt;

        P_ele = din * din * df_d;
        double sele = -df_t * ye;
        E_ele = ye * free_energy + T * sele;

        if (cv_ele != nullptr) {
            const double dti2 = dti * dti;
            const std::array<double, 6> wd{
                w0d, w0md, w1d, w1md, w2d, w2md};
            const std::array<double, 6> wt{
                ddpsi0(xt) * dti2,
                ddpsi0(1.0 - xt) * dti2,
                ddpsi1(xt) * dti,
                -ddpsi1(1.0 - xt) * dti,
                ddpsi2(xt),
                ddpsi2(1.0 - xt)};
            static constexpr int d_order[9]{0, 1, 0, 2, 0, 1, 2, 1, 2};
            static constexpr int t_order[9]{0, 0, 1, 0, 2, 1, 1, 2, 2};
            double df_tt = 0.0;
            for (int k = 0; k < 9; ++k) {
                for (int jd = 0; jd < 2; ++jd) {
                    for (int jt = 0; jt < 2; ++jt) {
                        const int idx = (j + jt) * imax + i + jd;
                        df_tt += f[k][idx] * wd[2 * d_order[k] + jd]
                                             * wt[2 * t_order[k] + jt];
                    }
                }
            }
            *cv_ele = -ye * T * df_tt;
        }
    }

    void calc_thermo_with_cv(double rho, double T, const double* X,
                             double& P, double& E, double* cv) const {
        // Timmes variables: ytot = sum(X/A), Abar = 1/ytot,
        // Zbar = sum(X Z/A)/ytot, and Ye = Zbar/Abar = sum(X Z/A).
        double ytot = 0.0;
        double ye = 0.0;
        for (int k = 0; k < specs->count(); ++k) {
            const double inv_A = 1.0 / specs->get_A(k);
            ytot += X[k] * inv_A;
            ye += X[k] * specs->get_Z(k) * inv_A;
        }
        ye = std::max(1.0e-16, ye);

        // 1. Electron/Positron from table
        double P_ele, E_ele, cv_ele = 0.0;
        interpolate_ele_pos(rho, T, ye, P_ele, E_ele,
                            cv != nullptr ? &cv_ele : nullptr);

        // 2. Ions (Ideal Gas)
        double n_ion = rho * ytot * avo;
        double P_ion = n_ion * kerg * T;
        double E_ion = 1.5 * P_ion / rho; // Specific internal energy

        // 3. Radiation
        double P_rad = asol / 3.0 * T * T * T * T;
        double E_rad = 3.0 * P_rad / rho;

        // 4. Timmes uniform-background Coulomb correction (Yakovlev &
        // Shalybkov 1989).  This is part of the original Helmholtz support
        // system and contributes to pressure, energy, and cv.
        constexpr double pi = 3.141592653589793238462643383279502884;
        constexpr double qe = 4.8032042712e-10;
        constexpr double a1 = -0.898004;
        constexpr double b1 = 0.96786;
        constexpr double c1 = 0.220703;
        constexpr double d1 = -0.86097;
        constexpr double a2 = 0.29561;
        constexpr double b2 = 1.9885;
        constexpr double c2 = 0.288675;
        constexpr double third = 1.0 / 3.0;

        const double zbar = ye / ytot;
        const double mean_ion_spacing =
            1.0 / std::cbrt((4.0 / 3.0) * pi * n_ion);
        const double coupling = zbar * zbar * qe * qe
                              / (kerg * T * mean_ion_spacing);
        const double dcoupling_dT = -coupling / T;

        double P_coul = 0.0;
        double E_coul = 0.0;
        double dE_coul_dT = 0.0;
        if (coupling >= 1.0) {
            const double g14 = std::pow(coupling, 0.25);
            const double coefficient = avo * ytot * kerg;
            E_coul = coefficient * T
                   * (a1 * coupling + b1 * g14 + c1 / g14 + d1);
            P_coul = third * rho * E_coul;
            const double dE_dg = coefficient * T
                * (a1 + 0.25 / coupling * (b1 * g14 - c1 / g14));
            dE_coul_dT = dE_dg * dcoupling_dT + E_coul / T;
        } else {
            const double g32 = coupling * std::sqrt(coupling);
            const double gb2 = std::pow(coupling, b2);
            const double correction = c2 * g32 - third * a2 * gb2;
            P_coul = -P_ion * correction;
            E_coul = 3.0 * P_coul / rho;
            const double dcorrection_dg =
                1.5 * c2 * g32 / coupling
                - third * a2 * b2 * gb2 / coupling;
            const double dP_coul_dT =
                -(P_ion / T) * correction
                - P_ion * dcorrection_dg * dcoupling_dT;
            dE_coul_dT = 3.0 * dP_coul_dT / rho;
        }

        // Match Timmes' bomb-proofing: disable the correction if it would
        // make pressure or internal energy non-positive.
        if (P_ele + P_ion + P_rad + P_coul <= 0.0
            || E_ele + E_ion + E_rad + E_coul <= 0.0) {
            P_coul = 0.0;
            E_coul = 0.0;
            dE_coul_dT = 0.0;
        }

        P = P_ele + P_ion + P_rad + P_coul;
        E = E_ele + E_ion + E_rad + E_coul;

        if (cv != nullptr) {
            *cv = cv_ele + 1.5 * avo * kerg * ytot
                + 4.0 * asol * T * T * T / rho + dE_coul_dT;
        }
    }

    void calc_thermo(double rho, double T, const double* X,
                     double& P, double& E) const {
        calc_thermo_with_cv(rho, T, X, P, E, nullptr);
    }

    // --- EOS Interface Implementation ---

    double get_gamma(const double* Xi) const {
        return 1.4; // Not strictly used for full real EOS formulation, but provided for interface
    }

    double get_pressure(const FluidVector& U, const double* Xi) const {
        double rho = U.rho;
        double e = eos_utils::extract_specific_internal_energy(U);
        return get_pressure_from_rho_e(rho, e, Xi);
    }

    double get_temperature(const FluidVector& U, const double* Xi) const {
        double rho = U.rho;
        double e = eos_utils::extract_specific_internal_energy(U);
        
        // Newton-Raphson to find T from (rho, e)
        double T_guess = 1e8; 
        for (int i = 0; i < 20; ++i) {
            double P, E;
            calc_thermo(rho, T_guess, Xi, P, E);
            
            const double cv = get_cv(rho, T_guess, Xi);
            
            if (std::abs(cv) < 1e-12) break;
            
            double dT_update = (e - E) / cv;
            T_guess += dT_update;
            
            // Limit update
            if (T_guess < 1e3) T_guess = 1e3;
            if (T_guess > 1e11) T_guess = 1e11;
            
            if (std::abs(dT_update) / T_guess < 1e-6) break;
        }
        return T_guess;
    }

    double get_temperature(double rho, double e, const double* Xi) const {
        double T_guess = 1e8; 
        for (int i = 0; i < 20; ++i) {
            double P, E;
            calc_thermo(rho, T_guess, Xi, P, E);
            
            const double cv = get_cv(rho, T_guess, Xi);
            
            if (std::abs(cv) < 1e-12) break;
            
            double dT_update = (e - E) / cv;
            T_guess += dT_update;
            
            if (T_guess < 1e3) T_guess = 1e3;
            if (T_guess > 1e11) T_guess = 1e11;
            
            if (std::abs(dT_update) / T_guess < 1e-6) break;
        }
        return T_guess;
    }

    double get_sound_speed(const FluidVector& U, double p, const double* Xi) const {
        double rho = U.rho;
        double e = eos_utils::extract_specific_internal_energy(U);
        double T = get_temperature(U, Xi);
        
        // c_s^2 = dp/drho |_S = dp/drho |_T + (dp/dT |_rho)^2 * T / (rho^2 cv)
        double P, E;
        calc_thermo(rho, T, Xi, P, E);
        
        double drho = rho * 1e-4;
        double P_rho, E_rho;
        calc_thermo(rho + drho, T, Xi, P_rho, E_rho);
        double dp_drho = (P_rho - P) / drho;
        
        double dT = T * 1e-4;
        double P_T, E_T;
        calc_thermo(rho, T + dT, Xi, P_T, E_T);
        double dp_dT = (P_T - P) / dT;
        double cv = get_cv(rho, T, Xi);
        
        double cs2 = dp_drho + dp_dT * dp_dT * T / (rho * rho * cv);
        return std::sqrt(std::max(1e-10, cs2));
    }

    double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double* Xi) const {
        return eos_utils::solve_total_energy(*this, rho, u, v, w, p, Xi);
    }

    double get_pressure_from_rho_e(double rho, double e, const double* Xi) const {
        // First find T
        double T_guess = 1e8; 
        for (int i = 0; i < 20; ++i) {
            double P, E;
            calc_thermo(rho, T_guess, Xi, P, E);
            
            const double cv = get_cv(rho, T_guess, Xi);
            
            if (std::abs(cv) < 1e-12) break;
            
            double dT_update = (e - E) / cv;
            T_guess += dT_update;
            
            if (T_guess < 1e3) T_guess = 1e3;
            if (T_guess > 1e11) T_guess = 1e11;
            
            if (std::abs(dT_update) / T_guess < 1e-6) break;
        }
        
        double P_final, E_final;
        calc_thermo(rho, T_guess, Xi, P_final, E_final);
        return P_final;
    }

    double get_pressure_from_rho_T(double rho, double T, const double* Xi) const {
        double P, E;
        calc_thermo(rho, T, Xi, P, E);
        return P;
    }

    double get_eint_from_T(double rho, double T, const double* Xi) const {
        double P, E;
        calc_thermo(rho, T, Xi, P, E);
        return E;
    }

    double get_cv(double rho, double T, const double* Xi) const {
        double P, E, cv;
        calc_thermo_with_cv(rho, T, Xi, P, E, &cv);
        return cv;
    }

    double get_dp_drho_e(double rho, double e, const double* Xi) const {
        // dp/drho |_e
        double P1 = get_pressure_from_rho_e(rho, e, Xi);
        double drho = rho * 1e-4;
        double P2 = get_pressure_from_rho_e(rho + drho, e, Xi);
        return (P2 - P1) / drho;
    }

    double get_dp_de_rho(double rho, double e, const double* Xi) const {
        // dp/de |_rho
        double P1 = get_pressure_from_rho_e(rho, e, Xi);
        double de = e * 1e-4;
        double P2 = get_pressure_from_rho_e(rho, e + de, Xi);
        return (P2 - P1) / de;
    }

    ~HelmEos() = default;
};
