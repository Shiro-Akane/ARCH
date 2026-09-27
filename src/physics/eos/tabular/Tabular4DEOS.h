/**
 * @file Tabular4DEOS.h
 * @brief 4D Tabular Equation of State reading from HDF5 (rho, T, A_bar, Z_bar).
 * Host and device views share interpolation and thermodynamic closure.
 */

/**
 * Workflow:
 * 1. Construct or query the configured thermodynamic closure from canonical state variables.
 * 2. Return pressure, temperature, and transport quantities with validated bounds.
 * 3. Keep host and device views consistent through one shared mathematical implementation.
 */
#pragma once

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "physics/eos/eos.h"
#include "physics/eos/eos_Utils.h"
#include "physics/eos/tabular/TabularFreeEnergy.h"
#include "physics/eos/tabular/TabularInversion.h"
#include "physics/eos/tabular/TabularInterpolation.h"

#include "physics/species/Species.h"
#include "physics/constant/PhysicalConstants.h"

// Keep the two large interpolation routines as explicit device call
// boundaries.  Force-inlining them into every reconstruction/flux query makes
// NVCC materialize the same 4-D/free-energy expression graph many times in a
// single hydro kernel, increasing compiler memory use. The formula
// remains shared by Host and CUDA; only the CUDA compilation boundary differs.
// ARCH_HEAVY_INLINE is owned by core/ArchPortability.h, shared with other EOS.

// One mathematical implementation parameterized by host or device species metadata.
template <class SpeciesView>
struct BasicTabular4DEOSView
{
    // Table dimensions and coordinate bounds for rho, temperature, Abar, and Zbar.
    int n_rho, n_T, n_A, n_Z;
    double log_rho_min, log_rho_max, dlog_rho;
    double log_T_min, log_T_max, dlog_T;
    double A_min, A_max, dA;
    double Z_min, Z_max, dZ;

    // Strict free-energy representation; source bounds are never extrapolated.
    double energy_reference_shift = 0.0;
    const double* table_valid = nullptr;
    std::array<const double*, tabular_eos::FieldCount> free_energy_fields{};
    SpeciesView specs{};

    // Optional non-owning device error latch, bound only on a per-launch view
    // copy.  Owners and Host views leave this null; Host failures still throw.
    int* device_error_status = nullptr;

    ARCH_INLINE std::size_t free_energy_index(
        int irho, int itemperature, int ia, int iz) const
    {
        const std::size_t composition_count =
            static_cast<std::size_t>(n_A) * n_Z;
        return static_cast<std::size_t>(irho) *
                   static_cast<std::size_t>(n_T) * composition_count +
               static_cast<std::size_t>(itemperature) * composition_count +
               static_cast<std::size_t>(ia) * n_Z +
               static_cast<std::size_t>(iz);
    }

    ARCH_INLINE bool valid_cell(int i, int j, int k, int l) const
    {
        if (!table_valid) return true;
        for (int dr = 0; dr < 2; ++dr)
            for (int dt = 0; dt < 2; ++dt)
                for (int da = 0; da < 2; ++da)
                    for (int dz = 0; dz < 2; ++dz)
                        if (table_valid[free_energy_index(i+dr, j+dt, k+da, l+dz)] != 1.0)
                            return false;
        return true;
    }

    ARCH_INLINE double strict_failure(tabular_eos::FreeEnergyStatus status) const
    {
        return tabular_eos::checked_thermodynamics(
            tabular_eos::free_energy_failure(status), device_error_status).energy;
    }

    ARCH_INLINE tabular_eos::FreeEnergyStatus strict_support(
        double rho, double T, double A, double Z) const
    {
        using Status = tabular_eos::FreeEnergyStatus;
        if (n_rho < 2 || n_T < 2 || n_A < 2 || n_Z < 2 ||
            !(dlog_rho > 0.0) || !(dlog_T > 0.0) || !(dA > 0.0) || !(dZ > 0.0) ||
            !std::isfinite(rho) || !std::isfinite(T) || !std::isfinite(A) || !std::isfinite(Z) ||
            !(rho > 0.0) || !(T > 0.0) ||
            rho < std::pow(10.0, log_rho_min) || rho > std::pow(10.0, log_rho_max) ||
            T < std::pow(10.0, log_T_min) || T > std::pow(10.0, log_T_max) ||
            A < A_min || A > A_max || Z < Z_min || Z > Z_max)
            return Status::invalid_native_domain;
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            if (!free_energy_fields[field]) return Status::invalid_native_cell;
        const auto ar = tabular_eos::locate_axis(std::log10(rho), n_rho, log_rho_min, dlog_rho, nullptr);
        const auto at = tabular_eos::locate_axis(std::log10(T), n_T, log_T_min, dlog_T, nullptr);
        const auto aa = tabular_eos::locate_axis(A, n_A, A_min, dA, nullptr);
        const auto az = tabular_eos::locate_axis(Z, n_Z, Z_min, dZ, nullptr);
        return valid_cell(ar.lower, at.lower, aa.lower, az.lower)
            ? Status::success : Status::invalid_native_cell;
    }

    ARCH_HEAVY_INLINE bool strict_thermal_polynomial(
        double rho, double A, double Z, int j, bool pressure,
        tabular_eos::ThermalPolynomial& out) const
    {
        const auto ar = tabular_eos::locate_axis(std::log10(rho), n_rho, log_rho_min, dlog_rho, nullptr);
        const auto aa = tabular_eos::locate_axis(A, n_A, A_min, dA, nullptr);
        const auto az = tabular_eos::locate_axis(Z, n_Z, Z_min, dZ, nullptr);
        const int i = ar.lower, k = aa.lower, l = az.lower;
        if (!valid_cell(i, j, k, l)) return false;
        const double hx = std::log(10.0) * dlog_rho, hy = std::log(10.0) * dlog_T;
        tabular_eos::ThermalPolynomial composition_patches[4];
        for (int a = 0; a < 2; ++a) {
            for (int z = 0; z < 2; ++z) {
                const std::array<std::size_t, 4> corners{
                    free_energy_index(i,j,k+a,l+z), free_energy_index(i,j+1,k+a,l+z),
                    free_energy_index(i+1,j,k+a,l+z), free_energy_index(i+1,j+1,k+a,l+z)};
                composition_patches[2*a+z] = tabular_eos::thermal_polynomial(
                    free_energy_fields, corners, ar.fraction, hx, hy, pressure ? 1 : 0);
            }
        }
        out = tabular_eos::blend(
            tabular_eos::blend(composition_patches[0], composition_patches[1], az.fraction),
            tabular_eos::blend(composition_patches[2], composition_patches[3], az.fraction), aa.fraction);
        if (pressure) {
            for (int degree = 0; degree < 6; ++degree) out[degree] *= rho;
        } else {
            const auto free_energy = out;
            for (int degree = 0; degree < 5; ++degree)
                out[degree] -= (degree + 1) * free_energy[degree + 1] / hy;
            if (energy_reference_shift != 0.0) out[0] += energy_reference_shift;
        }
        return true;
    }

    ARCH_HEAVY_INLINE double strict_temperature(
        double rho, double target, const double* Xi, bool pressure = false) const
    {
        const double A = get_Abar(Xi), Z = get_Zbar(Xi);
        const auto support = strict_support(rho, std::pow(10.0, log_T_min), A, Z);
        if (support == tabular_eos::FreeEnergyStatus::invalid_native_domain)
            return strict_failure(support);
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            if (!free_energy_fields[field]) return strict_failure(tabular_eos::FreeEnergyStatus::invalid_native_cell);
        const auto inverse = tabular_eos::invert_free_energy_temperature(
            n_T, log_T_min, dlog_T, target,
            [&](int j, tabular_eos::ThermalPolynomial& polynomial) {
                return strict_thermal_polynomial(rho, A, Z, j, pressure, polynomial);
            },
            [&](double T) {
                const auto f = interpolate_free_energy(rho, T, A, Z);
                return pressure ? rho * f.ax : tabular_eos::specific_energy(f, energy_reference_shift);
            },
            [&](double T) { return free_energy_result(rho, T, A, Z); }, log_T_max);
        if (inverse.status != tabular_eos::FreeEnergyStatus::success)
            return strict_failure(inverse.status);
        return inverse.temperature;
    }

    ARCH_HEAVY_INLINE tabular_eos::FreeEnergyState interpolate_free_energy(
        double rho, double T, double A, double Z,
        eos_utils::LinearCompositionDerivatives* derivatives = nullptr) const
    {
        const double log_rho = std::max(log_rho_min, std::min(std::log10(rho), log_rho_max));
        const double log_temperature = std::max(log_T_min, std::min(std::log10(T), log_T_max));
        int i = static_cast<int>((log_rho - log_rho_min) / dlog_rho);
        int j = static_cast<int>((log_temperature - log_T_min) / dlog_T);
        int k = static_cast<int>((A - A_min) / dA);
        int l = static_cast<int>((Z - Z_min) / dZ);
        i = std::max(0, std::min(i, n_rho - 2));
        j = std::max(0, std::min(j, n_T - 2));
        k = std::max(0, std::min(k, n_A - 2));
        l = std::max(0, std::min(l, n_Z - 2));

        const double tx =
            (log_rho - (log_rho_min + i * dlog_rho)) / dlog_rho;
        const double ty =
            (log_temperature - (log_T_min + j * dlog_T)) / dlog_T;
        const double ta = (A - (A_min + k * dA)) / dA;
        const double tz = (Z - (Z_min + l * dZ)) / dZ;
        const double hx = std::log(10.0) * dlog_rho;
        const double hy = std::log(10.0) * dlog_T;
        const std::array<std::size_t, 4> corners_00{
            free_energy_index(i, j, k, l),
            free_energy_index(i, j + 1, k, l),
            free_energy_index(i + 1, j, k, l),
            free_energy_index(i + 1, j + 1, k, l)};
        const std::array<std::size_t, 4> corners_01{
            free_energy_index(i, j, k, l + 1),
            free_energy_index(i, j + 1, k, l + 1),
            free_energy_index(i + 1, j, k, l + 1),
            free_energy_index(i + 1, j + 1, k, l + 1)};
        const std::array<std::size_t, 4> corners_10{
            free_energy_index(i, j, k + 1, l),
            free_energy_index(i, j + 1, k + 1, l),
            free_energy_index(i + 1, j, k + 1, l),
            free_energy_index(i + 1, j + 1, k + 1, l)};
        const std::array<std::size_t, 4> corners_11{
            free_energy_index(i, j, k + 1, l + 1),
            free_energy_index(i, j + 1, k + 1, l + 1),
            free_energy_index(i + 1, j, k + 1, l + 1),
            free_energy_index(i + 1, j + 1, k + 1, l + 1)};
        const auto state_00 = tabular_eos::interpolate_biquintic(
            free_energy_fields, corners_00, tx, ty, hx, hy, derivatives != nullptr);
        const auto state_01 = tabular_eos::interpolate_biquintic(
            free_energy_fields, corners_01, tx, ty, hx, hy, derivatives != nullptr);
        const auto state_10 = tabular_eos::interpolate_biquintic(
            free_energy_fields, corners_10, tx, ty, hx, hy, derivatives != nullptr);
        const auto state_11 = tabular_eos::interpolate_biquintic(
            free_energy_fields, corners_11, tx, ty, hx, hy, derivatives != nullptr);
        const auto lower_A = tabular_eos::blend(state_00, state_01, tz);
        const auto upper_A = tabular_eos::blend(state_10, state_11, tz);
        const auto state = tabular_eos::blend(lower_A, upper_A, ta);
        if (derivatives) {
            const auto energy = eos_utils::bilinear_value(
                state_00.a - state_00.ay, state_01.a - state_01.ay,
                state_10.a - state_10.ay, state_11.a - state_11.ay, ta, tz, dA, dZ);
            const auto capacity = eos_utils::bilinear_value(
                (state_00.ay - state_00.ayy) / T, (state_01.ay - state_01.ayy) / T,
                (state_10.ay - state_10.ayy) / T, (state_11.ay - state_11.ayy) / T, ta, tz, dA, dZ);
            *derivatives = {};
            derivatives->energy = {energy.first, energy.second};
            derivatives->energy_hessian[1] = energy.mixed;
            derivatives->cv = {capacity.first, capacity.second};
            derivatives->energy_temperature = derivatives->cv;
            derivatives->cv_temperature = (2.0 * state.ayy - state.ay - state.ayyy) / (T * T);
        }
        return state;
    }

    ARCH_INLINE tabular_eos::FreeEnergyResult free_energy_result(
        double rho, double T, double A, double Z) const
    {
        {
            const auto support = strict_support(rho, T, A, Z);
            if (support != tabular_eos::FreeEnergyStatus::success)
                return tabular_eos::free_energy_failure(support);
        }
        return tabular_eos::evaluate_thermodynamics(
            interpolate_free_energy(rho, T, A, Z), rho, T,
            energy_reference_shift);
    }

    ARCH_HEAVY_INLINE tabular_eos::ThermodynamicState free_energy_state(
        double rho, double T, double A, double Z) const
    {
        const auto result = free_energy_result(rho, T, A, Z);
        return tabular_eos::checked_thermodynamics(result, device_error_status);
    }

    // Composition coordinates Abar and Zbar.

    ARCH_INLINE double get_Abar(const double *Xi) const
    {
        if (specs.count > 0)
            return specs.calc_Abar(Xi);
        return strict_failure(tabular_eos::FreeEnergyStatus::invalid_native_domain);
    }

    ARCH_INLINE double get_Zbar(const double *Xi) const
    {
        if (specs.count > 0)
            return specs.calc_Zbar(Xi);
        return strict_failure(tabular_eos::FreeEnergyStatus::invalid_native_domain);
    }

    // Linear composition coordinates y=sum(X/A), z=Ye. Table coordinates
    // Abar=1/y and Zbar=z/y are differentiated here, before the species map.
    ARCH_INLINE std::array<double, 2> composition_weights(int species) const
    {
        if (species >= specs.count) return {};
        const double inverse_a = 1.0 / specs.get_A(species);
        return {inverse_a, specs.get_Z(species) * inverse_a};
    }

    ARCH_HEAVY_INLINE eos_utils::LinearCompositionDerivatives composition_derivatives(
        double rho, double T, const double* Xi) const
    {
        eos_utils::LinearCompositionDerivatives d{};
        {
            const auto state = free_energy_state(rho, T, get_Abar(Xi), get_Zbar(Xi));
            if (!std::isfinite(state.energy)) {
                d.energy.fill(state.energy); d.energy_temperature.fill(state.energy);
                d.cv.fill(state.energy); d.energy_hessian.fill(state.energy);
                d.cv_temperature = state.energy;
                return d;
            }
        }
        const double A = get_Abar(Xi), Z = get_Zbar(Xi);
        double y = 0.0;
        for (int i = 0; i < specs.count; ++i) y += Xi[i] / specs.get_A(i);
        const auto f = interpolate_free_energy(rho, T, A, Z, &d);
        const auto state = tabular_eos::checked_thermodynamics(
            tabular_eos::evaluate_thermodynamics(f, rho, T, energy_reference_shift), device_error_status);
        if (!std::isfinite(state.energy)) d.energy.fill(state.energy);
        const auto table = d;
        const double a_y = y > 0.0 ? -A * A : 0.0;
        const double z_y = y > 0.0 ? -A * Z : 0.0;
        d.energy = {table.energy[0] * a_y + table.energy[1] * z_y, table.energy[1] * A};
        d.energy_temperature = {table.energy_temperature[0] * a_y + table.energy_temperature[1] * z_y,
                                table.energy_temperature[1] * A};
        d.cv = {table.cv[0] * a_y + table.cv[1] * z_y, table.cv[1] * A};
        d.energy_hessian = {};
        if (y > 0.0) {
            d.energy_hessian[0] = 2.0 * A * A * (A * table.energy[0] + Z * table.energy[1])
                                + 2.0 * table.energy_hessian[1] * a_y * z_y;
            d.energy_hessian[1] = -A * A * (table.energy[1] + A * table.energy_hessian[1]);
        }
        return d;
    }

    template <int Equations>
    ARCH_INLINE void get_energy_composition_gradient(double rho, double T, const double* X, double* result) const
    { eos_utils::composition_derivative_query<Equations, eos_utils::CompositionQuery::energy_gradient>(*this, rho, T, X, result); }
    template <int Equations>
    ARCH_INLINE void get_cv_gradient(double rho, double T, const double* X, double* result) const
    { eos_utils::composition_derivative_query<Equations, eos_utils::CompositionQuery::cv_gradient>(*this, rho, T, X, result); }
    template <int Equations>
    ARCH_INLINE void get_energy_composition_hessian_action(double rho, double T, const double* X, const double* flow, double* result) const
    { eos_utils::composition_derivative_query<Equations, eos_utils::CompositionQuery::energy_hessian_action>(*this, rho, T, X, result, flow); }

ARCH_INLINE double get_pressure_from_rho_T(double rho, double T, const double *Xi) const
    {
        return free_energy_state(rho, T, get_Abar(Xi), get_Zbar(Xi)).pressure;
    }

ARCH_INLINE double get_pressure_from_rho_e(double rho, double e, const double *Xi) const
    {
        return free_energy_state(rho, strict_temperature(rho, e, Xi), get_Abar(Xi), get_Zbar(Xi)).pressure;
    }

ARCH_INLINE double get_eint_from_T(double rho, double T_target, const double *Xi) const
    {
        return free_energy_state(rho, T_target, get_Abar(Xi), get_Zbar(Xi)).energy;
    }

ARCH_INLINE double get_cv(double rho, double T_target, const double *Xi) const
    {
        return free_energy_state(rho, T_target, get_Abar(Xi), get_Zbar(Xi)).cv;
    }

ARCH_HEAVY_INLINE double get_temperature(
        double rho, double e, const double *Xi) const
    {
        return strict_temperature(rho, e, Xi);
    }

    ARCH_INLINE double get_pressure(const FluidVector &U, const double *Xi) const
    {
        double e_int = eos_utils::extract_specific_internal_energy(U);
        return get_pressure_from_rho_e(U.rho, e_int, Xi);
    }

ARCH_INLINE double get_sound_speed(const FluidVector &U, double p, const double *Xi) const
    {
        const double e = eos_utils::extract_specific_internal_energy(U);
        return free_energy_state(U.rho, strict_temperature(U.rho, e, Xi), get_Abar(Xi), get_Zbar(Xi)).sound_speed;
    }

ARCH_INLINE double get_gamma(const double *Xi, double rho = 0.0, double e = 0.0) const
    {
        return free_energy_state(rho, strict_temperature(rho, e, Xi), get_Abar(Xi), get_Zbar(Xi)).gamma1;
    }

ARCH_INLINE double get_sound_speed_from_rho_T(double rho, double T, const double *Xi) const
    {
        return free_energy_state(rho, T, get_Abar(Xi), get_Zbar(Xi)).sound_speed;
    }

ARCH_INLINE double get_dp_drho_e(double rho, double e, const double *Xi) const
    {
        return free_energy_state(rho, strict_temperature(rho, e, Xi), get_Abar(Xi), get_Zbar(Xi)).dp_drho_e;
    }

ARCH_INLINE double get_dp_de_rho(double rho, double e, const double *Xi) const
    {
        return free_energy_state(rho, strict_temperature(rho, e, Xi), get_Abar(Xi), get_Zbar(Xi)).dp_de_rho;
    }

ARCH_INLINE double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Xi) const
    {
        const double T = strict_temperature(rho, p, Xi, true);
        return rho * get_eint_from_T(rho, T, Xi) + eos_utils::calc_kinetic_energy(rho, u, v, w);
    }

    ARCH_INLINE double get_eta(double rho, double T, const double* Xi) const
    {
        {
            const auto state = free_energy_state(rho, T, get_Abar(Xi), get_Zbar(Xi));
            if (!std::isfinite(state.energy)) return state.energy;
        }
        return 0.0;
    }

    // Pipeline: evaluate_state
    ARCH_INLINE void evaluate_state(eos_state_t& state) const {
        {
            const auto t = free_energy_state(state.rho, state.T, get_Abar(state.Xi), get_Zbar(state.Xi));
            state.P=t.pressure; state.E=t.energy; state.cv=t.cv;
            state.sound_speed=t.sound_speed; state.dp_drho=t.dp_drho_e; state.dp_dT=t.dp_dT;
        }
        state.pele = state.xne = state.eta = 0.0;
    }

};

struct Tabular4DEOSHostView : BasicTabular4DEOSView<SpeciesHostView>
{
    std::size_t valid_extent = 0;
    std::array<std::size_t, tabular_eos::FieldCount> free_energy_extents{};
    const SpeciesManager *get_species_manager() const { return specs.host_owner; }
};

// Host owner for HDF5 loading and table lifetime.
struct Tabular4DEOS : public EOSBase
{
private:
    std::string table_path;
    std::vector<double> h_table_valid;

    std::array<std::vector<double>, tabular_eos::FieldCount> h_free_energy_fields;
    const SpeciesManager *specs_owner = nullptr;
    Tabular4DEOSHostView view;

public:
    Tabular4DEOS(const std::string &h5_filename, const SpeciesManager *specs_ptr = nullptr,
                const std::string& helm_path = "EOS_toolkit/tables/helmholtz/helm_table.dat");

    Tabular4DEOSHostView get_view() const
    {
        Tabular4DEOSHostView rebound = view;
        rebound.table_valid = h_table_valid.empty() ? nullptr : h_table_valid.data();
        rebound.valid_extent = h_table_valid.size();
        for (int field = 0; field < tabular_eos::FieldCount; ++field) {
            rebound.free_energy_fields[field] =
                h_free_energy_fields[field].empty()
                    ? nullptr : h_free_energy_fields[field].data();
            rebound.free_energy_extents[field] =
                h_free_energy_fields[field].size();
        }
        rebound.specs = specs_owner ? specs_owner->get_host_view() : SpeciesHostView{};
        return rebound;
    }
    const SpeciesManager* get_species_manager() const { return specs_owner; }
};
