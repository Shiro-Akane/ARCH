/**
 * @file HelmEos.h
 * @brief C++ adaptation of Frank Timmes's Helmholtz EOS.
 * @note The interpolation, thermodynamic formulas, and table layout trace to
 * the Helmholtz package at https://cococubed.com/code_pages/eos.shtml. The
 * runtime table is helm_table.dat from the project's downloaded
 * helmholtz.tar.xz archive; ARCH supplies the EOS policy and table checks.
 * Workflow:
 * 0. Stream the canonical decimal table into the original field layout,
 *    retaining exact conversion and rejecting incomplete or invalid input.
 * 1. Prepare the ordered composition sums first; build the density-axis and
 *    ion interpolation factors only when a query actually evaluates the EOS,
 *    so a repeated inverse-memo lookup can return before that work.
 * 2. Evaluate the shared electron/positron, ion, photon and Coulomb terms.
 * 3. Recover thermodynamic derivatives or a bounded temperature inverse;
 *    reuse work only while its complete state identity remains unchanged.
 * 4. Retain the pressure and the acoustic jet of one inverse state inside the
 *    same memo entry, so the pressure-only, sound-speed and derivative-pair
 *    queries of one (rho,e,X) endpoint never repeat the EOS evaluation.
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/CompensatedSum.h"
#include "data/FluidState.h"
#include "physics/constant/PhysicalConstants.h"
#include "physics/eos/eos.h"
#include "physics/eos/eos_Utils.h"
#include "physics/eos/sources/HelmTableReader.h"
#include "physics/species/Species.h"


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
    double coulomb_mult = 1.0; ///< Constant Helm correction fraction for this EOS owner.

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

    // Callers request only derivatives they consume. Every retained field
    // uses the same interpolation/contraction; Full remains the public default.
    enum class JetRequest { Acoustic, FirstLaw, Full };

    // One additive component in cgs units. Energy/free energy are specific
    // (erg/g); density and temperature derivatives hold Ye fixed. These
    // queries contain neither ideal ions nor ion-background Coulomb terms.
    struct ComponentThermodynamics {
        double pressure = 0.0, energy = 0.0, free_energy = 0.0, cv = 0.0;
        double pressure_density = 0.0, pressure_temperature = 0.0;
        double energy_density = 0.0, cv_temperature = 0.0;
        double entropy = 0.0; // Specific entropy, erg/(g K), from the potential derivative.
    };

    ARCH_INLINE static bool reject_component(ComponentThermodynamics& out) {
        const double invalid = std::numeric_limits<double>::quiet_NaN();
        out = {invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid, invalid};
        return false;
    }

    ARCH_INLINE static bool finite_component(const ComponentThermodynamics& value) {
        return std::isfinite(value.pressure) && std::isfinite(value.energy)
            && std::isfinite(value.free_energy) && std::isfinite(value.cv)
            && std::isfinite(value.pressure_density)
            && std::isfinite(value.pressure_temperature)
            && std::isfinite(value.energy_density) && std::isfinite(value.cv_temperature)
            && std::isfinite(value.entropy);
    }

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

    // Ordered composition sums y=sum(X/A), z=sum(X*Z/A) plus the charge flag
    // read BEFORE the Ye clamp. An inverse-memo key needs only this prefix;
    // the ion factors and density axis are a separate, later step so a cache
    // hit never builds them.
    struct CompositionState {
        double ytot = 0.0, ye = 0.0;
        bool charge_active = true;
    };

    // One inverse keeps rho and X fixed while only T changes. This local
    // workspace belongs to that call; it never survives an RK/ODE stage,
    // regrid, table replacement or checkpoint boundary.
    struct FixedDensityState : CompositionState {
        double n_ion, mean_ion_spacing;
        int index;
        double weights[6], first[6], second[6];
    };

    /** Ordered composition sums shared by the memo key and every EOS path. */
    ARCH_INLINE void prepare_composition(const double* X, CompositionState& state) const
    {
        for (int k = 0; k < specs.count; ++k) {
            const double inverse_a = 1.0 / specs.get_A(k);
            state.ytot += X[k] * inverse_a;
            state.ye += X[k] * specs.get_Z(k) * inverse_a;
        }
        state.charge_active = state.ye > 1.0e-16;
        state.ye = std::max(1.0e-16, state.ye);
    }

    /** Ion factors and quintic density basis for a prepared composition.
     *  Kept separate from prepare_composition so a memo hit skips the axis. */
    ARCH_INLINE void prepare_fixed_density_tail(double rho, FixedDensityState& state) const
    {
        state.n_ion = rho * state.ytot * avo;
        state.mean_ion_spacing = 1.0 / std::cbrt(
            (4.0 / 3.0) * arch::constants::math::pi * state.n_ion);
        prepare_density_axis(rho * state.ye, state);
    }

    /** Prepare composition and density-axis factors for an unchanged rho,X. */
    ARCH_INLINE FixedDensityState prepare_fixed_density(double rho, const double* X) const
    {
        FixedDensityState state{};
        prepare_composition(X, state);
        prepare_fixed_density_tail(rho, state);
        return state;
    }

    /** Form the same quintic density basis once for a fixed table coordinate. */
    ARCH_INLINE void prepare_density_axis(double density, FixedDensityState& state) const
    {
        const auto axis = locate_axis(density, density_nodes, imax, dlo, dstpi);
        state.index = axis.index;
        const double dd = axis.spacing, ddi = 1.0 / dd, xd = axis.coordinate;
        const double value[6]{1.0, psi0(1.0 - xd),
            psi1(xd) * dd, -psi1(1.0 - xd) * dd,
            psi2(xd) * dd * dd, psi2(1.0 - xd) * dd * dd};
        const double first[6]{0.0, -dpsi0(1.0 - xd) * ddi,
            dpsi1(xd), dpsi1(1.0 - xd),
            dpsi2(xd) * dd, -dpsi2(1.0 - xd) * dd};
        const double second[6]{0.0, ddpsi0(1.0 - xd) * ddi * ddi,
            ddpsi1(xd) * ddi, -ddpsi1(1.0 - xd) * ddi,
            ddpsi2(xd), ddpsi2(1.0 - xd)};
        for (int k = 0; k < 6; ++k) {
            state.weights[k] = value[k];
            state.first[k] = first[k];
            state.second[k] = second[k];
        }
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
    ARCH_HOST_DEVICE ARCH_FORCE_INLINE double hermite_row(const double (&values)[6],
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

    // The radiation contribution has one authority shared by the complete
    // Helmholtz EOS and selective tabular completion. Preserve the original
    // expressions and their evaluation order in the complete EOS.
    ARCH_INLINE static ComponentThermodynamics radiation_thermodynamics(double rho, double T) {
        ComponentThermodynamics result;
        result.pressure = asol / 3.0 * T * T * T * T;
        result.energy = 3.0 * result.pressure / rho;
        result.free_energy = -result.pressure / rho;
        result.cv = 4.0 * asol * T * T * T / rho;
        result.pressure_temperature = 4.0 * result.pressure / T;
        result.energy_density = -result.energy / rho;
        result.cv_temperature = 12.0 * asol * T * T / rho;
        result.entropy = result.cv / 3.0;
        return result;
    }

    // Unlike the complete EOS's historical extrapolation/clamping policy,
    // completion may use only actual electron-table support. A missing table,
    // zero net charge, unsupported rho*Ye/T or nonfinite result fails without
    // substituting another component. The caller owns the error context.
    ARCH_HEAVY_INLINE bool electron_positron_component(
        double rho, double T, double ye, ComponentThermodynamics& out) const {
        if (!std::isfinite(rho) || !std::isfinite(T) || !std::isfinite(ye)
            || !(rho > 0.0) || !(T > 0.0) || !(ye > 0.0) || ye > 1.0
            || !density_nodes || !temperature_nodes)
            return reject_component(out);
        const double electron_density = rho * ye;
        if (!std::isfinite(electron_density)
            || electron_density < density_nodes[0]
            || electron_density > density_nodes[imax - 1]
            || T < temperature_nodes[0] || T > temperature_nodes[jmax - 1])
            return reject_component(out);
        for (const double* field : f) if (!field) return reject_component(out);
        ComponentThermodynamics result;
        ThermodynamicDerivatives derivatives;
        interpolate_ele_pos(rho, T, ye, result.pressure, result.energy,
                            &result.cv, &derivatives, &result.free_energy, &result.entropy);
        result.pressure_density = derivatives.pressure_density;
        result.pressure_temperature = derivatives.pressure_temperature;
        result.energy_density = (result.pressure - T * result.pressure_temperature) / (rho * rho);
        result.cv_temperature = derivatives.cv_temperature;
        if (!finite_component(result)) return reject_component(out);
        out = result;
        return true;
    }

    // Photons require no electron table or composition metadata.
    ARCH_INLINE static bool photon_component(
        double rho, double T, ComponentThermodynamics& out) {
        if (!std::isfinite(rho) || !std::isfinite(T) || !(rho > 0.0) || !(T > 0.0))
            return reject_component(out);
        const auto result = radiation_thermodynamics(rho, T);
        if (!finite_component(result)) return reject_component(out);
        out = result;
        return true;
    }

    ARCH_HEAVY_INLINE void interpolate_ele_pos(double rho, double T, double ye,
                             double& P_ele, double& E_ele,
                             double* cv_ele = nullptr,
                             ThermodynamicDerivatives* derivatives = nullptr,
                             double* specific_free_energy = nullptr,
                             double* specific_entropy = nullptr,
                             const FixedDensityState* fixed = nullptr,
                             JetRequest request = JetRequest::Full) const {
        double din = rho * ye;
        FixedDensityState local{};
        if (fixed == nullptr) {
            prepare_density_axis(din, local);
            fixed = &local;
        }
        const auto temperature = locate_axis(T, temperature_nodes, jmax, tlo, tstpi);
        const int i = fixed->index, j = temperature.index;
        const double dth = temperature.spacing, dti = 1.0 / dth;
        const double xt = temperature.coordinate;
        const auto& wd = fixed->weights;
        const auto& wd_d = fixed->first;
        const auto& wd_dd = fixed->second;
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
                if (derivatives != nullptr && request == JetRequest::Full)
                    row_ttt[slot] = hermite_row(values, wt_ttt);
            }
        }
        const double free_energy = hermite_row(row, wd, true);
        const double df_d = hermite_row(row, wd_d, true);
        const double df_t = hermite_row(row_t, wd, true);

        P_ele = din * din * df_d;
        double sele = -df_t * ye;
        E_ele = ye * free_energy + T * sele;
        if (specific_free_energy != nullptr) *specific_free_energy = ye * free_energy;
        if (specific_entropy != nullptr) *specific_entropy = sele;

        if (cv_ele != nullptr) {
            *cv_ele = -ye * T * hermite_row(row_tt, wd, true);
        }
        if (derivatives != nullptr) {
            const double df_dd = hermite_row(row, wd_dd, true);
            const double df_dt = hermite_row(row_t, wd_d, true);
            *derivatives = {};
            derivatives->pressure_density = ye * (2.0 * din * df_d + din * din * df_dd);
            derivatives->pressure_temperature = din * din * df_dt;
            if (request != JetRequest::Acoustic)
                derivatives->energy_z = free_energy - T * df_t + din * (df_d - T * df_dt);
            if (request == JetRequest::Full) {
                const double df_ddt = hermite_row(row_t, wd_dd, true);
                const double df_tt = hermite_row(row_tt, wd, true);
                // Differentiate z*F_TT as one linear functional; subtracting
                // separately rounded terms loses pair-dominated residuals.
                double density_product_weights[6];
                for (int k = 0; k < 6; ++k)
                    density_product_weights[k] = std::fma(din, wd_d[k], wd[k]);
                derivatives->energy_zz = rho * (2.0 * (df_d - T * df_dt)
                                              + din * (df_dd - T * df_ddt));
                derivatives->cv_z = -T * hermite_row(row_tt, density_product_weights, true);
                derivatives->cv_temperature = -ye * (df_tt + T * hermite_row(row_ttt, wd, true));
            }
        }
    }

    ARCH_HEAVY_INLINE void calc_thermo_with_cv(double rho, double T, const double* X,
                             double& P, double& E, double* cv,
                             ThermodynamicDerivatives* derivatives = nullptr,
                             const FixedDensityState* fixed = nullptr,
                             JetRequest request = JetRequest::Full) const {
        // Timmes variables: ytot = sum(X/A), Abar = 1/ytot,
        // Zbar = sum(X Z/A)/ytot, and Ye = Zbar/Abar = sum(X Z/A).
        FixedDensityState local{};
        if (fixed == nullptr) {
            local = prepare_fixed_density(rho, X);
            fixed = &local;
        }
        const double ytot = fixed->ytot, ye = fixed->ye;
        const bool charge_active = fixed->charge_active;

        // 1. Electron/Positron from table
        double P_ele, E_ele, cv_ele = 0.0;
        interpolate_ele_pos(rho, T, ye, P_ele, E_ele,
                            cv != nullptr ? &cv_ele : nullptr, derivatives,
                            nullptr, nullptr, fixed, request);

        // 2. Ions (Ideal Gas)
        double n_ion = fixed->n_ion;
        double P_ion = n_ion * kerg * T;
        double E_ion = 1.5 * P_ion / rho; // Specific internal energy

        // 3. Radiation
        const auto radiation = radiation_thermodynamics(rho, T);
        const double P_rad = radiation.pressure;
        const double E_rad = radiation.energy;

        // 4. Timmes uniform-background Coulomb correction (Yakovlev &
        // Shalybkov 1989).  This is part of the original Helmholtz support
        // system and contributes to pressure, energy, and cv.
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
        const double mean_ion_spacing = fixed->mean_ion_spacing;
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
                if (request == JetRequest::Full)
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
                if (request == JetRequest::Full)
                    coupling_squared_f_second = -2.25 * c2 * g32 + b2 * (b2 - 1.0) * a2 * gb2;
            }
        }

        // A constant physical correction fraction scales the SAME free-energy
        // contribution and every derivative: Q = Q_ideal+electron+radiation
        // + f_C Q_C. Apply it before the inherited nonpositive-state guard.
        // f_C=1 preserves the original arithmetic; f_C=0 removes only Coulomb.
        P_coul *= coulomb_mult;
        E_coul *= coulomb_mult;
        dE_coul_dT *= coulomb_mult;
        coupling_f_first *= coulomb_mult;
        coupling_squared_f_second *= coulomb_mult;

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
                + radiation.cv + dE_coul_dT;
        }
        if (derivatives != nullptr) {
            auto& d = *derivatives;
            const double coefficient = avo * kerg;
            const double first_energy = coefficient * T * ytot * coupling_f_first;
            const double second_energy = coefficient * T * ytot * coupling_squared_f_second;
            d.pressure_density += P_ion / rho + E_coul / 3.0 + first_energy / 9.0;
            d.pressure_temperature += P_ion / T + radiation.pressure_temperature + rho * dE_coul_dT / 3.0;
            // Gamma is proportional to z^2 y^(-5/3) / T at fixed rho.
            if (request != JetRequest::Acoustic) {
                d.energy_y = 1.5 * coefficient * T + (E_coul - (5.0 / 3.0) * first_energy) / ytot;
                d.energy_z += 2.0 * first_energy / ye;
            }
            if (request == JetRequest::Full) {
                d.energy_yy = (10.0 * first_energy + 25.0 * second_energy) / (9.0 * ytot * ytot);
                d.energy_yz = -(4.0 * first_energy + 10.0 * second_energy) / (3.0 * ytot * ye);
                d.energy_zz += (2.0 * first_energy + 4.0 * second_energy) / (ye * ye);
                d.cv_y = 1.5 * coefficient + (E_coul - first_energy + (5.0 / 3.0) * second_energy) / (T * ytot);
                d.cv_z -= 2.0 * second_energy / (T * ye);
                d.cv_temperature += radiation.cv_temperature + second_energy / (T * T);
            }
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
        return arch::state::invalid(); // Real-EOS gamma requires a thermodynamic state.
    }

    ARCH_INLINE double get_pressure(const FluidVector& U, const double* Xi) const {
        double rho = U.rho;
        double e = eos_utils::extract_specific_internal_energy(U);
        return get_pressure_from_rho_e(rho, e, Xi);
    }

    ARCH_INLINE double get_temperature(const FluidVector& U, const double* Xi) const {
        return get_temperature(U.rho, eos_utils::extract_specific_internal_energy(U), Xi);
    }

    // A host patch-stage may lend bounded inverse-result storage. The EOS
    // owner and all table/species data remain fixed for this lexical scope.
    // Other host workers (including nested flux teams) do not inherit it.
    // Device evaluation calls the identical uncached inverse below.
    struct HostInverseWorkspace {
        struct Entry {
            std::array<std::uint64_t, 5> key{};
            double temperature = 0.0;
            bool ready = false;
            // Same entry, extended: the pressure and the acoustic jet at the
            // cached temperature. pressure_ready is set by a pressure-only
            // evaluation; acoustic_ready additionally promises cv and the two
            // fixed-composition pressure derivatives. A key change or slot
            // reuse clears the whole entry, so no field outlives its state.
            double pressure = 0.0;
            double cv = 0.0;
            double pressure_density = 0.0;
            double pressure_temperature = 0.0;
            bool pressure_ready = false;
            bool acoustic_ready = false;
        };
        const BasicHelmEosView* owner;
        std::array<Entry, 256> entries{};
        explicit HostInverseWorkspace(const BasicHelmEosView* value) : owner(value) {}

        /** Select a slot; every key word is still compared before a hit. */
        Entry& entry(const std::array<std::uint64_t, 5>& key) {
            std::uint64_t hash = 0;
            for (auto word : key) {
                hash ^= word;
                hash ^= hash >> 30; hash *= 0xbf58476d1ce4e5b9ULL;
                hash ^= hash >> 27; hash *= 0x94d049bb133111ebULL;
                hash ^= hash >> 31;
            }
            return entries[hash % entries.size()];
        }
    };
    inline static thread_local HostInverseWorkspace* host_inverse_workspace = nullptr;

    /** Keep exact reuse inside one host hydro evaluation, including exceptions. */
    class HostHydroScope {
        HostInverseWorkspace workspace_;
        HostInverseWorkspace* previous_;
    public:
        explicit HostHydroScope(const BasicHelmEosView& eos)
            : workspace_(&eos), previous_(host_inverse_workspace)
        { host_inverse_workspace = &workspace_; }
        ~HostHydroScope() { host_inverse_workspace = previous_; }
        HostHydroScope(const HostHydroScope&) = delete;
        HostHydroScope& operator=(const HostHydroScope&) = delete;
    };

    /** The ONE inverse memo: locate or fill the exact-key entry, and hand back
     *  its pointer so the temperature, pressure and acoustic fields of one
     *  (rho,target,X) identity share a single slot. Returns nullptr when the
     *  original strict inverse does not yield a reusable finite temperature. */
    ARCH_HEAVY_INLINE HostInverseWorkspace::Entry* inverse_memo(
        double rho, double target, const double* Xi, bool pressure,
        const CompositionState& composition) const {
#if !defined(__CUDA_ARCH__)
        // Helm's complete fixed-composition inverse depends on X only through
        // the original ordered sums y=sum(X/A), z=sum(X*Z/A). These are exact
        // floating-point keys, not rounded composition bins.
        const std::array<std::uint64_t, 5> key{
            std::bit_cast<std::uint64_t>(rho), std::bit_cast<std::uint64_t>(target),
            std::bit_cast<std::uint64_t>(composition.ytot),
            std::bit_cast<std::uint64_t>(composition.ye),
            static_cast<std::uint64_t>(pressure)};
        auto& slot = host_inverse_workspace->entry(key);
        if (slot.ready && slot.key == key) return &slot;
        const double result =
            invert_temperature_uncached(rho, target, Xi, pressure, &composition);
        // A failed query never overwrites success with a reusable sentinel.
        if (!(std::isfinite(result) && result > 0.0)) return nullptr;
        // A new key reuses the storage: clear the temperature and EVERY field
        // derived from it so a stale pressure/acoustic value can never leak.
        slot = {};
        slot.key = key;
        slot.temperature = result;
        slot.ready = true;
        return &slot;
#else
        (void)rho; (void)target; (void)Xi; (void)pressure; (void)composition;
        return nullptr;
#endif
    }

    /** Entry for one energy endpoint (rho,e,Xi), keyed exactly like the
     *  temperature memo so its cached pressure and acoustic fields always
     *  belong to the cached temperature. Returns nullptr when no workspace
     *  applies or the strict inverse did not succeed; the caller then runs the
     *  unchanged uncached path. */
    ARCH_HEAVY_INLINE HostInverseWorkspace::Entry* energy_state_entry(
        double rho, double e, const double* Xi) const {
#if !defined(__CUDA_ARCH__)
        if (host_inverse_workspace && host_inverse_workspace->owner == this
            && rho > 0.0 && e > 0.0 && std::isfinite(e)
            && Xi && specs.count > 0 && temperature_nodes) {
            CompositionState composition{};
            prepare_composition(Xi, composition);
            return inverse_memo(rho, e, Xi, false, composition);
        }
#endif
        return nullptr;
    }

    /** Retain the acoustic fields only while they are finite physical
     *  closures: a non-finite or nonpositive evaluation keeps its original
     *  return value but is never memoized as a successful state. */
    ARCH_INLINE void store_acoustic(HostInverseWorkspace::Entry& slot,
                                    double pressure, double cv,
                                    const ThermodynamicDerivatives& d) const {
        if (std::isfinite(pressure) && pressure > 0.0
            && std::isfinite(cv) && cv > 0.0
            && std::isfinite(d.pressure_density)
            && std::isfinite(d.pressure_temperature)) {
            slot.pressure = pressure;
            slot.cv = cv;
            slot.pressure_density = d.pressure_density;
            slot.pressure_temperature = d.pressure_temperature;
            slot.pressure_ready = slot.acoustic_ready = true;
        }
    }

    /** Recover T from the original root solve, reusing only identical inputs. */
    ARCH_HEAVY_INLINE double invert_temperature(double rho, double target,
                                                const double* Xi, bool pressure) const {
#if !defined(__CUDA_ARCH__)
        if (host_inverse_workspace && host_inverse_workspace->owner == this
            && rho > 0.0 && target > 0.0 && std::isfinite(target)
            && Xi && specs.count > 0 && temperature_nodes) {
            // Only the ordered sums are needed to key the lookup; the density
            // axis and ion factors stay unbuilt until the query actually
            // evaluates the EOS below, so a hit pays neither locate_axis nor
            // the quintic basis evaluation.
            CompositionState composition{};
            prepare_composition(Xi, composition);
            if (const auto* slot =
                    inverse_memo(rho, target, Xi, pressure, composition))
                return slot->temperature;
        }
#endif
        return invert_temperature_uncached(rho, target, Xi, pressure);
    }

    // Monotone bracket inversion on the actual source temperature domain.
    // This is the ARCH adapter, not a modification of the Timmes formulas.
    ARCH_HEAVY_INLINE double invert_temperature_uncached(double rho, double target,
        const double* Xi, bool pressure, const CompositionState* prepared = nullptr) const {
        if (!(rho > 0.0) || !(target > 0.0) || !std::isfinite(target)
            || !temperature_nodes || specs.count == 0 || Xi == nullptr)
            return arch::state::invalid();
        double lower = temperature_nodes[0];
        double upper = temperature_nodes[jmax - 1];
        // Lazy fixed-density construction: the caller may already hold the
        // exact composition prefix; the ion factors and density axis are built
        // here, once, only because this branch evaluates the EOS.
        FixedDensityState fixed{};
        if (prepared == nullptr) prepare_composition(Xi, fixed);
        else static_cast<CompositionState&>(fixed) = *prepared;
        prepare_fixed_density_tail(rho, fixed);
        const auto value = [&](double T, double& derivative) {
            double P, E, cv;
            ThermodynamicDerivatives d;
            // An energy inverse needs only e(T) and cv=de/dT. Request the
            // full thermodynamic jet only for the pressure inverse, whose
            // Newton derivative is dP/dT. This keeps the same bracket,
            // residual and cv arithmetic without unused third derivatives.
            calc_thermo_with_cv(rho, T, Xi, P, E, &cv,
                                pressure ? &d : nullptr, &fixed, JetRequest::Acoustic);
            derivative = pressure ? d.pressure_temperature : cv;
            return pressure ? P : E;
        };
        double derivative;
        const double lo_value = value(lower, derivative);
        const double hi_value = value(upper, derivative);
        if (!std::isfinite(lo_value) || !std::isfinite(hi_value)
            || target < lo_value || target > hi_value) return arch::state::invalid();
        if (target == lo_value) return lower;
        if (target == hi_value) return upper;
        double T = std::max(lower, std::min(1e8, upper));
        for (int iteration = 0; iteration < 96; ++iteration) {
            const double current = value(T, derivative);
            const double residual = current - target;
            if (!std::isfinite(current) || !(derivative > 0.0)
                || !std::isfinite(derivative)) return arch::state::invalid();
            const double correction = residual / derivative;
            // Iterate directly in temperature: exp(log(T)) loses low bits even
            // when T already solves the equation. Require both a small residual
            // and a resolved temperature correction, especially for degeneracy.
            if (std::abs(residual) <= 64.0 * std::numeric_limits<double>::epsilon()
                                      * std::max(std::abs(target), std::abs(current))
                && std::abs(correction) <= std::numeric_limits<double>::epsilon()*T)
                return T;
            if (residual < 0.0) lower = T; else upper = T;
            const double trial = T - correction;
            const double next = trial > lower && trial < upper ? trial
                : std::sqrt(lower)*std::sqrt(upper);
            if (next == T || upper-lower <= 4.0*std::numeric_limits<double>::epsilon()*T) {
                // EOS evaluation itself may round across a root. A collapsed
                // bracket is accepted only with the same residual gate.
                if (std::abs(residual) <= 64.0*std::numeric_limits<double>::epsilon()
                        * std::max(std::abs(target),std::abs(current))) return T;
                break;
            }
            T = next;
        }
        return arch::state::invalid();
    }

    ARCH_HEAVY_INLINE double get_temperature(double rho, double e, const double* Xi) const {
        return invert_temperature(rho, e, Xi, false);
    }

    ARCH_INLINE double sound_speed(double rho, double T, double cv,
                                    const ThermodynamicDerivatives& d) const {
        // Fixed-composition first law: c_s^2 = P_rho + T P_T^2/(rho^2 cv).
        if (!(cv > 0.0)) return std::numeric_limits<double>::quiet_NaN();
        return std::sqrt(d.pressure_density
            + T * (d.pressure_temperature / rho) * (d.pressure_temperature / rho) / cv);
    }

    // Helm derives c_s directly from rho,e,X; a separately computed pressure
    // is unnecessary for CFL queries. Both public signatures use this leaf.
    ARCH_INLINE double get_sound_speed(const FluidVector& U, const double* Xi) const {
        const double T = get_temperature(U, Xi);
        double P, E, cv;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(U.rho, T, Xi, P, E, &cv, &d, nullptr, JetRequest::Acoustic);
        return sound_speed(U.rho, T, cv, d);
    }

    ARCH_INLINE double get_sound_speed(const FluidVector& U, double, const double* Xi) const {
        return get_sound_speed(U, Xi);
    }

    // One strict inverse supplies the pressure and acoustic derivative of the
    // SAME (rho,e,X) endpoint. The general-EOS c^2 = chi + P*kappa/rho^2
    // is algebraically identical to the two scalar derivative getters below,
    // but avoids repeating the Helm temperature inversion three times.
    ARCH_INLINE void get_pressure_and_sound_speed(
        double rho, double e, const double* Xi,
        double& pressure, double& speed) const
    {
        auto* slot = energy_state_entry(rho, e, Xi);
        const double T =
            slot != nullptr ? slot->temperature : get_temperature(rho, e, Xi);
        if (!std::isfinite(T)) {
            pressure = speed = arch::state::invalid();
            return;
        }
        double energy, cv;
        ThermodynamicDerivatives d;
        if (slot != nullptr && slot->acoustic_ready) {
            pressure = slot->pressure;
            cv = slot->cv;
            d.pressure_density = slot->pressure_density;
            d.pressure_temperature = slot->pressure_temperature;
        } else {
            calc_thermo_with_cv(rho, T, Xi, pressure, energy, &cv, &d, nullptr, JetRequest::Acoustic);
            if (slot != nullptr) store_acoustic(*slot, pressure, cv, d);
        }
        if (!(cv > 0.0) || !std::isfinite(pressure)) {
            speed = arch::state::invalid();
            return;
        }
        const double energy_density =
            (pressure - T * d.pressure_temperature) / (rho * rho);
        const double chi = d.pressure_density
            - d.pressure_temperature * energy_density / cv;
        const double kappa = d.pressure_temperature / cv;
        speed = std::sqrt(chi + (kappa / rho) * (pressure / rho));
    }

    // All composition and heat-capacity derivatives below come from the
    // same Helm state jet. The grouped burn queries evaluate that jet once
    // per unchanged (rho,T,X) trial state, while the scalar entry points keep
    // their existing public contract.
    template <int Equations>
    ARCH_INLINE void energy_composition_gradient_from_derivatives(
        const ThermodynamicDerivatives& d, double* gradient) const
    {
        for (int i = 0; i < Equations - 1; ++i) {
            const double inverse_a = i < specs.count ? 1.0 / specs.get_A(i) : 0.0;
            const double charge = i < specs.count && d.charge_active ? specs.get_Z(i) : 0.0;
            gradient[i] = inverse_a * (d.energy_y + charge * d.energy_z);
        }
    }

    template <int Equations>
    ARCH_INLINE void energy_composition_hessian_from_derivatives(
        const ThermodynamicDerivatives& d, const double* flow,
        double* action) const
    {
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
    ARCH_INLINE void cv_gradient_from_derivatives(
        const ThermodynamicDerivatives& d, double* gradient) const
    {
        for (int i = 0; i < Equations - 1; ++i) {
            const double inverse_a = i < specs.count ? 1.0 / specs.get_A(i) : 0.0;
            const double charge = i < specs.count && d.charge_active ? specs.get_Z(i) : 0.0;
            gradient[i] = inverse_a * (d.cv_y + charge * d.cv_z);
        }
        gradient[Equations - 1] = d.cv_temperature;
    }

    // Borrowed for ONE ODE Jacobian assembly. The derivative jet has exactly
    // the caller's rho,T,X identity; no accepted-state or temporal cache exists.
    struct BurnThermodynamics {
        const BasicHelmEosView* owner;
        ThermodynamicDerivatives derivatives;
        double capacity;

        /** Supply eta through the original EOS rate-input query. */
        ARCH_INLINE double get_eta(double rho, double T, const double* X) const
        { return owner->get_eta(rho, T, X); }

        /** Reuse cv and e_X from the same trial-state derivative jet. */
        template<int Equations>
        ARCH_INLINE void get_cv_and_energy_composition_gradient(
            double, double, const double*, double& cv, double* gradient) const
        {
            cv = capacity;
            owner->template energy_composition_gradient_from_derivatives<Equations>(
                derivatives, gradient);
        }

        /** Contract the existing Hessian with X' without another interpolation. */
        template<int Equations>
        ARCH_INLINE void get_energy_composition_hessian_action(
            double, double, const double*, const double* flow, double* action) const
        {
            owner->template energy_composition_hessian_from_derivatives<Equations>(
                derivatives, flow, action);
        }

        /** Supply cv gradients and the e_X Hessian action from one jet. */
        template<int Equations>
        ARCH_INLINE void get_cv_gradient_and_energy_composition_hessian_action(
            double, double, const double*, const double* flow,
            double* gradient, double* action) const
        {
            owner->template cv_gradient_from_derivatives<Equations>(derivatives, gradient);
            owner->template energy_composition_hessian_from_derivatives<Equations>(
                derivatives, flow, action);
        }
    };

    /** Prepare derivatives once for the RHS and thermal Jacobian at one state. */
    ARCH_INLINE BurnThermodynamics burn_thermodynamics(
        double rho, double T, const double* X) const
    {
        BurnThermodynamics result{this, {}, 0.0};
        double pressure, energy;
        calc_thermo_with_cv(rho, T, X, pressure, energy,
                            &result.capacity, &result.derivatives);
        return result;
    }

    template <int Equations>
    ARCH_INLINE void get_cv_and_energy_composition_gradient(
        double rho, double T, const double* X, double& cv,
        double* gradient) const
    {
        double P, E;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(rho, T, X, P, E, &cv, &d, nullptr, JetRequest::FirstLaw);
        energy_composition_gradient_from_derivatives<Equations>(d, gradient);
    }

    template <int Equations>
    ARCH_INLINE void get_cv_gradient_and_energy_composition_hessian_action(
        double rho, double T, const double* X, const double* flow,
        double* cv_gradient, double* action) const
    {
        double P, E;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(rho, T, X, P, E, nullptr, &d);
        cv_gradient_from_derivatives<Equations>(d, cv_gradient);
        energy_composition_hessian_from_derivatives<Equations>(d, flow, action);
    }

    template <int Equations>
    ARCH_INLINE void get_energy_composition_gradient(
        double rho, double T, const double* X, double* gradient) const
    {
        double P, E;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(rho, T, X, P, E, nullptr, &d, nullptr, JetRequest::FirstLaw);
        energy_composition_gradient_from_derivatives<Equations>(d, gradient);
    }

    template <int Equations>
    ARCH_INLINE void get_energy_composition_hessian_action(
        double rho, double T, const double* X, const double* flow,
        double* action) const
    {
        double P, E;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(rho, T, X, P, E, nullptr, &d);
        energy_composition_hessian_from_derivatives<Equations>(d, flow, action);
    }

    template <int Equations>
    ARCH_INLINE void get_cv_gradient(
        double rho, double T, const double* X, double* gradient) const
    {
        double P, E;
        ThermodynamicDerivatives d;
        calc_thermo_with_cv(rho, T, X, P, E, nullptr, &d);
        cv_gradient_from_derivatives<Equations>(d, gradient);
    }

    ARCH_INLINE double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double* Xi) const {
        const double T = invert_temperature(rho, p, Xi, true);
        if (!std::isfinite(T)) return T;
        return rho * get_eint_from_T(rho, T, Xi) + eos_utils::calc_kinetic_energy(rho, u, v, w);
    }

    ARCH_INLINE double get_pressure_from_rho_e(double rho, double e, const double* Xi) const {
        // Exact reuse of an earlier identical (rho,e,Xi) query. A pressure-only
        // miss evaluates the unchanged pressure path and never builds the
        // acoustic jet.
        if (auto* slot = energy_state_entry(rho, e, Xi)) {
            if (slot->pressure_ready) return slot->pressure;
            double P, E;
            calc_thermo(rho, slot->temperature, Xi, P, E);
            if (std::isfinite(P) && P > 0.0) {
                slot->pressure = P;
                slot->pressure_ready = true;
            }
            return P;
        }
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
        auto* slot = energy_state_entry(rho, e, Xi);
        const double T =
            slot != nullptr ? slot->temperature : get_temperature(rho, e, Xi);
        double P, cv;
        ThermodynamicDerivatives d;
        if (slot != nullptr && slot->acoustic_ready) {
            P = slot->pressure;
            cv = slot->cv;
            d.pressure_density = slot->pressure_density;
            d.pressure_temperature = slot->pressure_temperature;
        } else {
            double E;
            calc_thermo_with_cv(rho, T, Xi, P, E, &cv, &d, nullptr, JetRequest::Acoustic);
            if (slot != nullptr) store_acoustic(*slot, P, cv, d);
        }
        const double energy_density = (P - T * d.pressure_temperature) / (rho * rho);
        return d.pressure_density - d.pressure_temperature * energy_density / cv;
    }

    ARCH_INLINE double get_dp_de_rho(double rho, double e, const double* Xi) const {
        auto* slot = energy_state_entry(rho, e, Xi);
        const double T =
            slot != nullptr ? slot->temperature : get_temperature(rho, e, Xi);
        double cv;
        ThermodynamicDerivatives d;
        if (slot != nullptr && slot->acoustic_ready) {
            cv = slot->cv;
            d.pressure_temperature = slot->pressure_temperature;
        } else {
            double P, E;
            calc_thermo_with_cv(rho, T, Xi, P, E, &cv, &d, nullptr, JetRequest::Acoustic);
            if (slot != nullptr) store_acoustic(*slot, P, cv, d);
        }
        return d.pressure_temperature / cv;
    }

    // Roe's two derivatives at one unchanged (rho,e,X) state share the same
    // strict temperature inversion and Helm derivative jet. Keep the scalar
    // formulas and public scalar entries above for all other callers.
    ARCH_INLINE void get_dp_drho_e_and_dp_de_rho(
        double rho, double e, const double* Xi,
        double& chi, double& kappa) const
    {
        auto* slot = energy_state_entry(rho, e, Xi);
        const double T =
            slot != nullptr ? slot->temperature : get_temperature(rho, e, Xi);
        double P, cv;
        ThermodynamicDerivatives d;
        if (slot != nullptr && slot->acoustic_ready) {
            P = slot->pressure;
            cv = slot->cv;
            d.pressure_density = slot->pressure_density;
            d.pressure_temperature = slot->pressure_temperature;
        } else {
            double E;
            calc_thermo_with_cv(rho, T, Xi, P, E, &cv, &d, nullptr, JetRequest::Acoustic);
            if (slot != nullptr) store_acoustic(*slot, P, cv, d);
        }
        const double energy_density =
            (P - T * d.pressure_temperature) / (rho * rho);
        chi = d.pressure_density
            - d.pressure_temperature * energy_density / cv;
        kappa = d.pressure_temperature / cv;
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
    HelmEos(const std::string &table_path, const SpeciesManager *species_owner,
            double correction_fraction = 1.0)
    {
        if (!std::isfinite(correction_fraction) || correction_fraction < 0.0 || correction_fraction > 1.0)
            throw std::invalid_argument("Helm Coulomb fraction must be in [0,1]");
        coulomb_mult = correction_fraction;
        std::cout << "[HelmEos] Loading 2D Helmholtz table from "
                  << table_path << "..." << std::endl;
        std::ifstream file(table_path);
        if (!file.is_open())
            throw std::runtime_error("Could not open helm_table.dat at " + table_path);

        for (int k = 0; k < 9; ++k) host_f[k].resize(imax * jmax);
        for (int k = 0; k < 4; ++k) host_ef_table[k].resize(imax * jmax);

        // Only byte traversal changes: the four original loops below still
        // consume all 21 fields in the documented Timmes table order.
        helm_eos::loader::HelmTableReader table_reader(file);
        const auto read_value = [&](double &value) {
            if (!table_reader.read(value))
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
