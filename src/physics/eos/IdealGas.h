/**
 * @file IdealGas.h
 * @brief Equation of State (EOS) solver for an Ideal Gas.
 */

/**
 * Workflow:
 * 1. Construct or query the configured thermodynamic closure from canonical state variables.
 * 2. Return pressure, temperature, and transport quantities with validated bounds.
 * 3. Keep host and future device views consistent through one dispatch contract.
 */

#pragma once

#include <algorithm> // for std::max
#include <cmath>

#include "eos.h"
#include "eos_Utils.h"

#include "../species/Species.h"

struct IdealGas : public EOSBase
{
    const SpeciesManager manager;
    double global_gamma;

    IdealGas(const SpeciesManager &m) : manager(m), global_gamma(1.4) {}
    IdealGas(double default_gamma, const SpeciesManager &m)
        : manager(m), global_gamma(default_gamma) {}

    const SpeciesManager* get_species_manager() const { return &manager; }

    // Mixture thermodynamic properties.

    double get_gamma(const double *Xi) const
    {
        if (manager.count() == 0)
            return global_gamma;

        double sum_Xi_Cv = 0.0;
        double sum_Xi_Cv_gm1 = 0.0;

        for (int k = 0; k < manager.count(); ++k)
        {
            if (Xi[k] > 1e-12)
            {
                // SpeciesManager supplies immutable per-species gamma values.
                double gamma_i = manager.get_gamma_ref(k);
                double Cv_i = manager.get_Cv_ref(k);

                sum_Xi_Cv += Xi[k] * Cv_i;
                sum_Xi_Cv_gm1 += Xi[k] * Cv_i * (gamma_i - 1.0);
            }
        }

        if (sum_Xi_Cv < 1e-12)
            return global_gamma;
        return (sum_Xi_Cv_gm1 / sum_Xi_Cv) + 1.0;
    }

    double get_mixture_Cv(const double *Xi) const
    {
        if (manager.count() == 0)
            return 718.0; // Air-like Cv fallback in J/(kg K) when no species exist.

        double cv_mix = 0.0;
        for (int k = 0; k < manager.count(); ++k)
        {
            cv_mix += Xi[k] * manager.get_Cv_ref(k);
        }
        return cv_mix;
    }

    // Policy interface shared with tabular EOS views.
    const IdealGas &get_view() const
    {
        return *this;
    }

    double get_pressure_from_rho_T(double rho, double T, const double *Xi) const
    {
        double cv_mix = get_mixture_Cv(Xi);
        return (get_gamma(Xi) - 1.0) * rho * cv_mix * T;
    }

    double get_pressure_from_rho_e(double rho, double e, const double *Xi) const
    {
        return (get_gamma(Xi) - 1.0) * rho * e;
    }

    // Temperature interface required by all reaction-network solvers.
    double get_temperature(double rho, double e, const double *Xi) const
    {
        // Ideal-gas relation e=Cv*T, hence T=e/Cv.
        double cv_mix = get_mixture_Cv(Xi);
        if (cv_mix < 1e-12)
            return 0.0;
        return e / cv_mix;
    }

    double get_eint_from_T(double rho, double T, const double *Xi) const
    {
        // Analytic ideal-gas relation e=Cv*T.
        double cv_mix = get_mixture_Cv(Xi);
        return cv_mix * T;
    }

    double get_cv(double rho, double T, const double *Xi) const
    {
        // Cv is composition-dependent but temperature-independent for an ideal gas.
        return get_mixture_Cv(Xi);
    }

    double get_eta(double rho, double T, const double* Xi) const { return 0.0; }

    ~IdealGas() = default;

    double get_pressure(const FluidVector &U, const double *Xi) const
    {
        if (U.rho < 1e-12)
            return 0.0;
        double e_int = eos_utils::extract_specific_internal_energy(U);
        return std::max(0.0, get_pressure_from_rho_e(U.rho, e_int, Xi));
    }

    double get_sound_speed(const FluidVector &U, double p, const double *Xi) const
    {
        if (U.rho < 1e-12)
            return 0.0;
        return std::sqrt(get_gamma(Xi) * p / U.rho);
    }

    double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Xi) const
    {
        double gamma_mix = get_gamma(Xi);
        double e_internal_vol = p / (gamma_mix - 1.0);
        return e_internal_vol + eos_utils::calc_kinetic_energy(rho, u, v, w);
    }

    // Analytic derivatives used by implicit Jacobians.

    double get_dp_drho_e(double rho, double e, const double *Xi) const
    {
        return (get_gamma(Xi) - 1.0) * e;
    }

    double get_dp_de_rho(double rho, double e, const double *Xi) const
    {
        return (get_gamma(Xi) - 1.0) * rho;
    }

    void evaluate_state(eos_state_t& state) const {
        state.P = get_pressure_from_rho_T(state.rho, state.T, state.Xi);
        state.E = get_eint_from_T(state.rho, state.T, state.Xi);
        state.cv = get_cv(state.rho, state.T, state.Xi);
        state.sound_speed = std::sqrt(get_gamma(state.Xi) * state.P / state.rho);
        state.dp_drho = get_dp_drho_e(state.rho, state.E, state.Xi);
        state.dp_dT = state.rho * state.cv * (get_gamma(state.Xi) - 1.0); // simple ideal gas dp/dT

        state.pele = 0.0;
        state.xne = 0.0;
        state.eta = 0.0;
    }
};
