/**
 * @file HelmEos.h
 * @brief C++ adaptation of Frank Timmes's Helmholtz EOS.
 * @note The interpolation, thermodynamic formulas, and table layout trace to
 * the Helmholtz package at https://cococubed.com/code_pages/eos.shtml. The
 * runtime table is helm_table.dat from the project's downloaded
 * helmholtz.tar.xz archive; ARCH supplies the EOS policy and table checks.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "eos.h"
#include "eos_Utils.h"

#include "../../data/FluidState.h"
#include "../../core/CompensatedSum.h"
#include "../species/Species.h"
#include "../constant/PhysicalConstants.h"


// Timmes Helmholtz EOS leaf parameterized by host or device species metadata.
template <class SpeciesView>
struct BasicHelmEosView {
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

    const double *f[9]{};
    const double *ef_table[4]{};
    const double *density_nodes = nullptr;
    const double *temperature_nodes = nullptr;
    SpeciesView specs{};

    // Fixed-composition pressure derivatives and composition coordinates
    // y=sum(X/A), z=sum(X Z/A). These are derivatives of the SAME energy
    // interpolant/corrections, not a second EOS or finite pressure stencil.
    struct ThermodynamicDerivatives {
        double pressure_density = 0.0, pressure_temperature = 0.0;
        double energy_y = 0.0, energy_z = 0.0;
        double energy_yy = 0.0, energy_yz = 0.0, energy_zz = 0.0;
        double cv_y = 0.0, cv_z = 0.0, cv_temperature = 0.0;
        bool charge_active = true;
    };

    struct AxisCell { int index; double spacing, coordinate; };

    ARCH_INLINE AxisCell locate_axis(double value, const double* nodes,
                                     int count, double log_lower,
                                     double inverse_log_step) const {
        const double log_value = std::max(log_lower,
            std::min(std::log10(value), log_lower + (count - 1) / inverse_log_step));
        int index = std::max(0, std::min(
            static_cast<int>((log_value - log_lower) * inverse_log_step), count - 2));
        // log10 only estimates the cell; the immutable node values decide
        // ties on both backends, including roundoff at exact table nodes.
        while (index > 0 && value < nodes[index]) --index;
        while (index < count - 2 && value >= nodes[index + 1]) ++index;
        const double spacing = nodes[index + 1] - nodes[index];
        return {index, spacing, std::max((value - nodes[index]) / spacing, 0.0)};
    }

    // Quintic Hermite polynomials
    // Factored endpoint zeros avoid cancellation near z=1. These are the
    // same quintic basis polynomials, not an EOS approximation or new table.
    ARCH_INLINE double psi0(double z) const {
        const double s = 1.0 - z;
        return s*s*s * std::fma(z, std::fma(6.0, z, 3.0), 1.0);
    }
    ARCH_INLINE double dpsi0(double z) const {
        const double s = 1.0 - z;
        return -30.0*z*z*s*s;
    }
    ARCH_INLINE double ddpsi0(double z) const { return -60.0*z*(1.0 - z)*(1.0 - 2.0*z); }

    ARCH_INLINE double psi1(double z) const {
        const double s = 1.0 - z;
        return z*s*s*s * std::fma(3.0, z, 1.0);
    }
    ARCH_INLINE double dpsi1(double z) const {
        const double s = 1.0 - z;
        return s*s * std::fma(z, std::fma(-15.0, z, 2.0), 1.0);
    }
    ARCH_INLINE double ddpsi1(double z) const { return -12.0*z*(1.0 - z)*std::fma(-5.0, z, 3.0); }

    ARCH_INLINE double psi2(double z) const {
        const double s = 1.0 - z;
        return 0.5*z*z*s*s*s;
    }
    ARCH_INLINE double dpsi2(double z) const {
        const double s = 1.0 - z;
        return 0.5*z*s*s*std::fma(-5.0, z, 2.0);
    }
    ARCH_INLINE double ddpsi2(double z) const { return (1.0 - z)*std::fma(z, std::fma(10.0, z, -8.0), 1.0); }
    ARCH_INLINE double dddpsi0(double z) const { return -60.0 + z * (360.0 - 360.0 * z); }
    ARCH_INLINE double dddpsi1(double z) const { return -36.0 + z * (192.0 - 180.0 * z); }
    ARCH_INLINE double dddpsi2(double z) const { return -9.0 + z * (36.0 - 30.0 * z); }

    // Contract one Hermite axis after removing its constant background.
    // The two value bases sum to one (and their derivatives to zero).
    // Enforcing that identity avoids subtracting large degenerate-electron
    // energies when evaluating a small temperature derivative. Values are
    // ordered as lower/upper value, first derivative, second derivative;
    // weights[0] is one for a value query and zero for a derivative query.
    ARCH_INLINE double hermite_row(const double (&values)[6],
                                   const double (&weights)[6],
                                   bool upper_is_difference = false) const {
        arch::math::CompensatedSum sum;
        sum.add_product(values[0], weights[0]);
        sum.add_product(upper_is_difference ? values[1] : values[1] - values[0], weights[1]);
        for (int k = 2; k < 6; ++k)
            sum.add_product(values[k], weights[k]);
        return sum.value();
    }

    // Formula notation aliases the shared constants; table data are unchanged.
    static constexpr double kerg = arch::constants::statistical::cgs::boltzmann;
    static constexpr double avo = arch::constants::statistical::avogadro;
    static constexpr double asol = arch::constants::radiation::cgs::energy_density;

    ARCH_HEAVY_INLINE void interpolate_ele_pos(double rho, double T, double ye,
                             double& P_ele, double& E_ele,
                             double* cv_ele = nullptr,
                             ThermodynamicDerivatives* derivatives = nullptr) const {
        double din = rho * ye;
        const auto density = locate_axis(din, density_nodes, imax, dlo, dstpi);
        const auto temperature = locate_axis(T, temperature_nodes, jmax, tlo, tstpi);
        const int i = density.index, j = temperature.index;
        const double dd = density.spacing, dth = temperature.spacing;
        const double ddi = 1.0 / dd, dti = 1.0 / dth;
        const double xd = density.coordinate, xt = temperature.coordinate;

        const double wd[6]{1.0, psi0(1.0 - xd),
            psi1(xd) * dd, -psi1(1.0 - xd) * dd,
            psi2(xd) * dd * dd, psi2(1.0 - xd) * dd * dd};
        const double wd_d[6]{0.0, -dpsi0(1.0 - xd) * ddi,
            dpsi1(xd), dpsi1(1.0 - xd),
            dpsi2(xd) * dd, -dpsi2(1.0 - xd) * dd};
        const double wd_dd[6]{0.0, ddpsi0(1.0 - xd) * ddi * ddi,
            ddpsi1(xd) * ddi, -ddpsi1(1.0 - xd) * ddi,
            ddpsi2(xd), ddpsi2(1.0 - xd)};
        const double wt[6]{1.0, psi0(1.0 - xt),
            psi1(xt) * dth, -psi1(1.0 - xt) * dth,
            psi2(xt) * dth * dth, psi2(1.0 - xt) * dth * dth};
        const double wt_t[6]{0.0, -dpsi0(1.0 - xt) * dti,
            dpsi1(xt), dpsi1(1.0 - xt),
            dpsi2(xt) * dth, -dpsi2(1.0 - xt) * dth};
        const double wt_tt[6]{0.0, ddpsi0(1.0 - xt) * dti * dti,
            ddpsi1(xt) * dti, -ddpsi1(1.0 - xt) * dti,
            ddpsi2(xt), ddpsi2(1.0 - xt)};
        const double wt_ttt[6]{0.0, -dddpsi0(1.0 - xt) * dti * dti * dti,
            dddpsi1(xt) * dti * dti, dddpsi1(1.0 - xt) * dti * dti,
            dddpsi2(xt) * dti, -dddpsi2(1.0 - xt) * dti};

        // Table slots for (density derivative, temperature derivative).
        // First contract temperature at each density endpoint/derivative,
        // then density. This is the same tensor-product quintic polynomial.
        constexpr int field[3][3]{{0, 2, 4}, {1, 5, 7}, {3, 6, 8}};
        double row[6], row_t[6], row_tt[6], row_ttt[6];
        for (int dr = 0; dr < 3; ++dr) {
            for (int side = 0; side < 2; ++side) {
                const int lower = j * imax + i + side;
                double values[6];
                for (int dt = 0; dt < 3; ++dt) {
                    values[2 * dt] = f[field[dr][dt]][lower];
                    values[2 * dt + 1] = f[field[dr][dt]][lower + imax];
                    if (dr == 0 && side == 1) {
                        // Form the density-endpoint difference before the
                        // temperature contraction rounds either large value.
                        // This preserves the same tensor polynomial while
                        // conditioning mixed and second-density derivatives.
                        values[2 * dt] -= f[field[dr][dt]][lower - 1];
                        values[2 * dt + 1] -= f[field[dr][dt]][lower + imax - 1];
                    }
                }
                const int slot = 2 * dr + side;
                row[slot] = hermite_row(values, wt);
                row_t[slot] = hermite_row(values, wt_t);
                if (cv_ele != nullptr || derivatives != nullptr)
                    row_tt[slot] = hermite_row(values, wt_tt);
                if (derivatives != nullptr)
                    row_ttt[slot] = hermite_row(values, wt_ttt);
            }
        }
        const double free_energy = hermite_row(row, wd, true);
        const double df_d = hermite_row(row, wd_d, true);
        const double df_t = hermite_row(row_t, wd, true);

        P_ele = din * din * df_d;
        double sele = -df_t * ye;
        E_ele = ye * free_energy + T * sele;

        if (cv_ele != nullptr) {
            *cv_ele = -ye * T * hermite_row(row_tt, wd, true);
        }
        if (derivatives != nullptr) {
            const double df_dd = hermite_row(row, wd_dd, true);
            const double df_dt = hermite_row(row_t, wd_d, true);
            const double df_ddt = hermite_row(row_t, wd_dd, true);
            const double df_tt = hermite_row(row_tt, wd, true);
            // Differentiate z*F_TT as one linear functional. Separately
            // rounding F_TT and d*F_dTT loses their small residual when
            // electron/positron pairs dominate the heat capacity.
            double density_product_weights[6];
            for (int k = 0; k < 6; ++k)
                density_product_weights[k] = std::fma(din, wd_d[k], wd[k]);
            *derivatives = {};
            derivatives->pressure_density = ye * (2.0 * din * df_d + din * din * df_dd);
            derivatives->pressure_temperature = din * din * df_dt;
            derivatives->energy_z = free_energy - T * df_t + din * (df_d - T * df_dt);
            derivatives->energy_zz = rho * (2.0 * (df_d - T * df_dt)
                                          + din * (df_dd - T * df_ddt));
            derivatives->cv_z = -T * hermite_row(row_tt, density_product_weights, true);
            derivatives->cv_temperature = -ye * (df_tt + T * hermite_row(row_ttt, wd, true));
        }
    }

    ARCH_HEAVY_INLINE void calc_thermo_with_cv(double rho, double T, const double* X,
                             double& P, double& E, double* cv,
                             ThermodynamicDerivatives* derivatives = nullptr) const {
        // Timmes variables: ytot = sum(X/A), Abar = 1/ytot,
        // Zbar = sum(X Z/A)/ytot, and Ye = Zbar/Abar = sum(X Z/A).
        double ytot = 0.0;
        double ye = 0.0;
        for (int k = 0; k < specs.count; ++k) {
            const double inv_A = 1.0 / specs.get_A(k);
            ytot += X[k] * inv_A;
            ye += X[k] * specs.get_Z(k) * inv_A;
        }
        const bool charge_active = ye > 1.0e-16;
        ye = std::max(1.0e-16, ye);

        // 1. Electron/Positron from table
        double P_ele, E_ele, cv_ele = 0.0;
        interpolate_ele_pos(rho, T, ye, P_ele, E_ele,
                            cv != nullptr ? &cv_ele : nullptr, derivatives);

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
        constexpr double pi = arch::constants::math::pi;
        constexpr double qe_squared =
            arch::constants::electromagnetic::cgs::elementary_charge_squared;
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
        const double coupling = zbar * zbar * qe_squared
                              / (kerg * T * mean_ion_spacing);
        const double dcoupling_dT = -coupling / T;

        double P_coul = 0.0;
        double E_coul = 0.0;
        double dE_coul_dT = 0.0;
        double coupling_f_first = 0.0, coupling_squared_f_second = 0.0;
        if (coupling >= 1.0) {
            const double g14 = std::pow(coupling, 0.25);
            const double coefficient = avo * ytot * kerg;
            E_coul = coefficient * T
                   * (a1 * coupling + b1 * g14 + c1 / g14 + d1);
            P_coul = third * rho * E_coul;
            const double dE_dg = coefficient * T
                * (a1 + 0.25 / coupling * (b1 * g14 - c1 / g14));
            dE_coul_dT = dE_dg * dcoupling_dT + E_coul / T;
            if (derivatives != nullptr) {
                coupling_f_first = a1 * coupling + 0.25 * (b1 * g14 - c1 / g14);
                coupling_squared_f_second = (-3.0 * b1 * g14 + 5.0 * c1 / g14) / 16.0;
            }
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
            if (derivatives != nullptr) {
                coupling_f_first = -4.5 * c2 * g32 + b2 * a2 * gb2;
                coupling_squared_f_second = -2.25 * c2 * g32 + b2 * (b2 - 1.0) * a2 * gb2;
            }
        }

        // Match Timmes' bomb-proofing: disable the correction if it would
        // make pressure or internal energy non-positive.
        if (P_ele + P_ion + P_rad + P_coul <= 0.0
            || E_ele + E_ion + E_rad + E_coul <= 0.0) {
            P_coul = 0.0;
            E_coul = 0.0;
            dE_coul_dT = 0.0;
            coupling_f_first = coupling_squared_f_second = 0.0;
        }

        P = P_ele + P_ion + P_rad + P_coul;
        E = E_ele + E_ion + E_rad + E_coul;

        if (cv != nullptr) {
            *cv = cv_ele + 1.5 * avo * kerg * ytot
                + 4.0 * asol * T * T * T / rho + dE_coul_dT;
        }
        if (derivatives != nullptr) {
            auto& d = *derivatives;
            const double coefficient = avo * kerg;
            const double first_energy = coefficient * T * ytot * coupling_f_first;
            const double second_energy = coefficient * T * ytot * coupling_squared_f_second;
            d.pressure_density += P_ion / rho + E_coul / 3.0 + first_energy / 9.0;
            d.pressure_temperature += P_ion / T + 4.0 * P_rad / T + rho * dE_coul_dT / 3.0;
            // Gamma is proportional to z^2 y^(-5/3) / T at fixed rho.
            d.energy_y = 1.5 * coefficient * T + (E_coul - (5.0 / 3.0) * first_energy) / ytot;
            d.energy_z += 2.0 * first_energy / ye;
            d.energy_yy = (10.0 * first_energy + 25.0 * second_energy) / (9.0 * ytot * ytot);
            d.energy_yz = -(4.0 * first_energy + 10.0 * second_energy) / (3.0 * ytot * ye);
            d.energy_zz += (2.0 * first_energy + 4.0 * second_energy) / (ye * ye);
            d.cv_y = 1.5 * coefficient + (E_coul - first_energy + (5.0 / 3.0) * second_energy) / (T * ytot);
            d.cv_z -= 2.0 * second_energy / (T * ye);
            d.cv_temperature += 12.0 * asol * T * T / rho + second_energy / (T * T);
            d.charge_active = charge_active;
        }
    }

    ARCH_INLINE void calc_thermo(double rho, double T, const double* X,
                     double& P, double& E) const {
        calc_thermo_with_cv(rho, T, X, P, E, nullptr);
    }

    // --- EOS Interface Implementation ---

    ARCH_INLINE double get_eta(double rho, double T, const double* Xi) const {
        double ytot = 0.0;
        double ye = 0.0;
        for (int k = 0; k < specs.count; ++k) {
            const double inv_A = 1.0 / specs.get_A(k);
            ytot += Xi[k] * inv_A;
            ye += Xi[k] * specs.get_Z(k) * inv_A;
        }
        ye = std::max(1.0e-16, ye);

        double din = rho * ye;
        const auto density = locate_axis(din, density_nodes, imax, dlo, dstpi);
        const auto temperature = locate_axis(T, temperature_nodes, jmax, tlo, tstpi);
        const int i = density.index, j = temperature.index;
        const double dd = density.spacing, dth = temperature.spacing;
        const double xd = density.coordinate, xt = temperature.coordinate;

        // Bicubic Hermite interpolation basis functions
        auto h00 = [](double z) { return (2.0 * z * z * z - 3.0 * z * z + 1.0); };
        auto h10 = [](double z) { return (z * z * z - 2.0 * z * z + z); };
        auto h01 = [](double z) { return (-2.0 * z * z * z + 3.0 * z * z); };
        auto h11 = [](double z) { return (z * z * z - z * z); };

        double w0d = h00(xd), w1d = h10(xd) * dd;
        double w2d = h01(xd), w3d = h11(xd) * dd;

        double w0t = h00(xt), w1t = h10(xt) * dth;
        double w2t = h01(xt), w3t = h11(xt) * dth;

        int idx00 = j * imax + i;
        int idx10 = j * imax + i + 1;
        int idx01 = (j + 1) * imax + i;
        int idx11 = (j + 1) * imax + i + 1;

        double efi[16];
        int offset[4] = {0, 4, 8, 12};
        for (int k = 0; k < 4; ++k) {
            int off = offset[k];
            efi[off + 0] = ef_table[k][idx00];
            efi[off + 1] = ef_table[k][idx10];
            efi[off + 2] = ef_table[k][idx01];
            efi[off + 3] = ef_table[k][idx11];
        }

        double etaele =
              efi[0]*w0d*w0t  + efi[1]*w2d*w0t  + efi[2]*w0d*w2t  + efi[3]*w2d*w2t
            + efi[4]*w1d*w0t  + efi[5]*w3d*w0t  + efi[6]*w1d*w2t  + efi[7]*w3d*w2t
            + efi[8]*w0d*w1t  + efi[9]*w2d*w1t  + efi[10]*w0d*w3t + efi[11]*w2d*w3t
            + efi[12]*w1d*w1t + efi[13]*w3d*w1t + efi[14]*w1d*w3t + efi[15]*w3d*w3t;

        return etaele;
    }

    ARCH_INLINE double get_gamma(const double* Xi) const {
        return 1.4; // Not strictly used for full real EOS formulation, but provided for interface
    }

    ARCH_INLINE double get_pressure(const FluidVector& U, const double* Xi) const {
        double rho = U.rho;
        double e = eos_utils::extract_specific_internal_energy(U);
        return get_pressure_from_rho_e(rho, e, Xi);
    }

    ARCH_INLINE double get_temperature(const FluidVector& U, const double* Xi) const {
        double rho = U.rho;
        double e = eos_utils::extract_specific_internal_energy(U);
        return get_temperature(rho, e, Xi);
    }

    ARCH_HEAVY_INLINE double get_temperature(double rho, double e, const double* Xi) const {
        double T_guess = 1e8;
        for (int i = 0; i < 50; ++i) {
            double P, E;
            calc_thermo(rho, T_guess, Xi, P, E);

            const double cv = get_cv(rho, T_guess, Xi);

            if (std::abs(cv) < 1e-12) break;

            double dT_update = (e - E) / cv;

            // Limit the temperature update to prevent overshoot and divergence
            double dT_limited = std::max(-0.5 * T_guess, std::min(dT_update, 0.5 * T_guess));
            T_guess += dT_limited;

            if (T_guess < 1e3) T_guess = 1e3;
            if (T_guess > 1e11) T_guess = 1e11;

            if (std::abs(dT_limited) / T_guess < 1e-6) break;
        }
        return T_guess;
    }

    ARCH_INLINE double sound_speed(double rho, double T, double cv,
                                    const ThermodynamicDerivatives& d) const {
        // Fixed-composition first law: c_s^2 = P_rho + T P_T^2/(rho^2 cv).
        if (!(cv > 0.0)) return std::numeric_limits<double>::quiet_NaN();
        return std::sqrt(d.pressure_density
            + T * d.pressure_temperature * d.pressure_temperature / (rho * rho * cv));
    }

    ARCH_INLINE double get_sound_speed(const FluidVector& U, double, const double* Xi) const {
        const double T = get_temperature(U, Xi);
        double P, E, cv;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(U.rho, T, Xi, P, E, &cv, &d);
        return sound_speed(U.rho, T, cv, d);
    }

    template <int Equations>
    ARCH_INLINE void get_energy_composition_gradient(
        double rho, double T, const double* X, double* gradient) const {
        double P, E;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(rho, T, X, P, E, nullptr, &d);
        for (int i = 0; i < Equations - 1; ++i) {
            const double inverse_a = i < specs.count ? 1.0 / specs.get_A(i) : 0.0;
            const double charge = i < specs.count && d.charge_active ? specs.get_Z(i) : 0.0;
            gradient[i] = inverse_a * (d.energy_y + charge * d.energy_z);
        }
    }

    template <int Equations>
    ARCH_INLINE void get_energy_composition_hessian_action(
        double rho, double T, const double* X, const double* flow, double* action) const {
        double P, E;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(rho, T, X, P, E, nullptr, &d);
        arch::math::CompensatedSum y_flow, z_flow;
        for (int i = 0; i < Equations - 1 && i < specs.count; ++i) {
            y_flow.add(flow[i] / specs.get_A(i));
            if (d.charge_active) z_flow.add(flow[i] * specs.get_Z(i) / specs.get_A(i));
        }
        const double hy = d.energy_yy * y_flow.value() + d.energy_yz * z_flow.value();
        const double hz = d.energy_yz * y_flow.value() + d.energy_zz * z_flow.value();
        for (int i = 0; i < Equations - 1; ++i) {
            const double inverse_a = i < specs.count ? 1.0 / specs.get_A(i) : 0.0;
            const double charge = i < specs.count && d.charge_active ? specs.get_Z(i) : 0.0;
            action[i] = inverse_a * (hy + charge * hz);
        }
        action[Equations - 1] = d.cv_y * y_flow.value() + d.cv_z * z_flow.value();
    }

    template <int Equations>
    ARCH_INLINE void get_cv_gradient(double rho, double T, const double* X, double* gradient) const {
        double P, E;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(rho, T, X, P, E, nullptr, &d);
        for (int i = 0; i < Equations - 1; ++i) {
            const double inverse_a = i < specs.count ? 1.0 / specs.get_A(i) : 0.0;
            const double charge = i < specs.count && d.charge_active ? specs.get_Z(i) : 0.0;
            gradient[i] = inverse_a * (d.cv_y + charge * d.cv_z);
        }
        gradient[Equations - 1] = d.cv_temperature;
    }

    ARCH_INLINE double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double* Xi) const {
        return eos_utils::solve_total_energy(*this, rho, u, v, w, p, Xi);
    }

    ARCH_INLINE double get_pressure_from_rho_e(double rho, double e, const double* Xi) const {
        double T = get_temperature(rho, e, Xi);
        double P, E;
        calc_thermo(rho, T, Xi, P, E);
        return P;
    }

    ARCH_INLINE double get_pressure_from_rho_T(double rho, double T, const double* Xi) const {
        double P, E;
        calc_thermo(rho, T, Xi, P, E);
        return P;
    }

    ARCH_INLINE double get_eint_from_T(double rho, double T, const double* Xi) const {
        double P, E;
        calc_thermo(rho, T, Xi, P, E);
        return E;
    }

    ARCH_INLINE double get_cv(double rho, double T, const double* Xi) const {
        double P, E, cv;
        calc_thermo_with_cv(rho, T, Xi, P, E, &cv);
        return cv;
    }

    ARCH_INLINE double get_dp_drho_e(double rho, double e, const double* Xi) const {
        const double T = get_temperature(rho, e, Xi);
        double P, E, cv;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(rho, T, Xi, P, E, &cv, &d);
        const double energy_density = (P - T * d.pressure_temperature) / (rho * rho);
        return d.pressure_density - d.pressure_temperature * energy_density / cv;
    }

    ARCH_INLINE double get_dp_de_rho(double rho, double e, const double* Xi) const {
        const double T = get_temperature(rho, e, Xi);
        double P, E, cv;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(rho, T, Xi, P, E, &cv, &d);
        return d.pressure_temperature / cv;
    }

    // Pipeline: evaluate_state
    ARCH_INLINE void evaluate_state(eos_state_t& state) const {
        // 1. Core Thermodynamics (P, E, cv)
        double P, E, cv;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(state.rho, state.T, state.Xi, P, E, &cv, &d);
        state.P = P;
        state.E = E;
        state.cv = cv;

        // 2. Derivatives and Sound Speed
        state.dp_drho = d.pressure_density;
        state.dp_dT = d.pressure_temperature;
        state.sound_speed = sound_speed(state.rho, state.T, cv, d);

        // 3. Deep Physical Variables (eta, pele, xne)
        state.eta = get_eta(state.rho, state.T, state.Xi);

        double ytot = 0.0;
        double ye = 0.0;
        for (int k = 0; k < specs.count; ++k) {
            const double inv_A = 1.0 / specs.get_A(k);
            ytot += state.Xi[k] * inv_A;
            ye += state.Xi[k] * specs.get_Z(k) * inv_A;
        }
        ye = std::max(1.0e-16, ye);

        double pele, E_ele;
        interpolate_ele_pos(state.rho, state.T, ye, pele, E_ele, nullptr);
        state.pele = pele;
        state.xne = state.rho * ye * avo;
    }

};


struct HelmEosHostView : BasicHelmEosView<SpeciesHostView>
{
    std::array<std::size_t, 9> f_extents{};
    std::array<std::size_t, 4> ef_extents{};
    std::array<std::size_t, 2> node_extents{};
    const SpeciesManager *get_species_manager() const { return specs.host_owner; }
};

// Host owner for canonical file parsing and table lifetime.
class HelmEos : public EOSBase, public HelmEosHostView
{
    std::vector<double> host_f[9];
    std::vector<double> host_ef_table[4];
    std::array<double, imax> host_density_nodes{};
    std::array<double, jmax> host_temperature_nodes{};

public:
    HelmEos(const std::string &table_path, const SpeciesManager *species_owner)
    {
        std::cout << "[HelmEos] Loading 2D Helmholtz table from "
                  << table_path << "..." << std::endl;
        std::ifstream file(table_path);
        if (!file.is_open())
            throw std::runtime_error("Could not open helm_table.dat at " + table_path);

        for (int k = 0; k < 9; ++k) host_f[k].resize(imax * jmax);
        for (int k = 0; k < 4; ++k) host_ef_table[k].resize(imax * jmax);

        const auto read_value = [&](double &value) {
            if (!(file >> value))
                throw std::runtime_error(
                    "Incomplete or nonnumeric 541x201 Timmes helm_table.dat: "
                    + table_path);
        };

        for (int j = 0; j < jmax; ++j) {
            for (int i = 0; i < imax; ++i) {
                const int index = j * imax + i;
                for (int k = 0; k < 9; ++k) read_value(host_f[k][index]);
            }
        }
        for (int j = 0; j < jmax; ++j) {
            for (int i = 0; i < imax; ++i) {
                double unused;
                for (int k = 0; k < 4; ++k) read_value(unused);
            }
        }
        for (int j = 0; j < jmax; ++j) {
            for (int i = 0; i < imax; ++i) {
                const int index = j * imax + i;
                for (int k = 0; k < 4; ++k)
                    read_value(host_ef_table[k][index]);
            }
        }
        for (int j = 0; j < jmax; ++j) {
            for (int i = 0; i < imax; ++i) {
                double unused;
                for (int k = 0; k < 4; ++k) read_value(unused);
            }
        }

        std::cout << "[HelmEos] Loaded complete 541x201 electron/positron table."
                  << std::endl;

        for (int k = 0; k < 9; ++k) {
            f[k] = host_f[k].data();
            f_extents[k] = host_f[k].size();
        }
        for (int k = 0; k < 4; ++k) {
            ef_table[k] = host_ef_table[k].data();
            ef_extents[k] = host_ef_table[k].size();
        }
        // Materialize the documented grid once. Device owners copy these
        // values with the table; libm/libdevice pow rounding must not move
        // physical interpolation endpoints or be paid on every EOS query.
        // Construct the decimal exponent before the final binary64 rounding.
        // Rounding i*0.05 first shifts some nodes by several ULPs; second
        // derivatives amplify that coordinate error in pair-dominated states.
        for (int i = 0; i < imax; ++i)
            host_density_nodes[i] = static_cast<double>(std::pow(10.0L,
                static_cast<long double>(dlo) + static_cast<long double>(i) / dstpi));
        for (int j = 0; j < jmax; ++j)
            host_temperature_nodes[j] = static_cast<double>(std::pow(10.0L,
                static_cast<long double>(tlo) + static_cast<long double>(j) / tstpi));
        density_nodes = host_density_nodes.data();
        temperature_nodes = host_temperature_nodes.data();
        node_extents = {host_density_nodes.size(), host_temperature_nodes.size()};
        specs = species_owner ? species_owner->get_host_view() : SpeciesHostView{};
    }

    HelmEos(const HelmEos&) = delete;
    HelmEos& operator=(const HelmEos&) = delete;
    HelmEos(HelmEos&&) = delete;
    HelmEos& operator=(HelmEos&&) = delete;

    HelmEosHostView get_view() const
    {
        return static_cast<const HelmEosHostView &>(*this);
    }
};
