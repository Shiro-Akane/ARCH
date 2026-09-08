/**
 * @file IdealGas.h
 * @brief Calorically perfect ideal-gas mixture closure and its Host owner.
 *
 * IdealGasView contains the shared thermodynamic leaves and non-owning species
 * data. The Host policy binds species ownership to that view. Per-species heat
 * capacities and gamma values determine the mixture; an empty species view
 * uses the configured global gamma and the documented fallback heat capacity.
 */

#pragma once

#include <algorithm> // for std::max
#include <cmath>
#include <vector>

#include "eos.h"
#include "eos_Utils.h"

#include "../species/Species.h"

struct IdealGasView
{
    // Air-like model fallback in J/(kg K), not a universal physical constant.
    static constexpr double default_specific_heat_cv = 718.0;
    SpeciesPODView species{};
    double global_gamma = 1.4;

    // Mixture thermodynamic properties.

    ARCH_INLINE double get_gamma(const double *Xi) const
    {
        if (species.count == 0)
            return global_gamma;

        double sum_Xi_Cv = 0.0;
        double sum_Xi_Cv_gm1 = 0.0;

        for (int k = 0; k < species.count; ++k)
        {
            if (Xi[k] > 1e-12)
            {
                // SpeciesManager supplies immutable per-species gamma values.
                double gamma_i = species.get_gamma_ref(k);
                double Cv_i = species.get_Cv_ref(k);

                sum_Xi_Cv += Xi[k] * Cv_i;
                sum_Xi_Cv_gm1 += Xi[k] * Cv_i * (gamma_i - 1.0);
            }
        }

        if (sum_Xi_Cv < 1e-12)
            return global_gamma;
        return (sum_Xi_Cv_gm1 / sum_Xi_Cv) + 1.0;
    }

    ARCH_INLINE double get_mixture_Cv(const double *Xi) const
    {
        if (species.count == 0)
            return default_specific_heat_cv;

        double cv_mix = 0.0;
        for (int k = 0; k < species.count; ++k)
        {
            cv_mix += Xi[k] * species.get_Cv_ref(k);
        }
        return cv_mix;
    }

    ARCH_INLINE double get_pressure_from_rho_T(double rho, double T, const double *Xi) const
    {
        double cv_mix = get_mixture_Cv(Xi);
        return (get_gamma(Xi) - 1.0) * rho * cv_mix * T;
    }

    ARCH_INLINE double get_pressure_from_rho_e(double rho, double e, const double *Xi) const
    {
        return (get_gamma(Xi) - 1.0) * rho * e;
    }

    // Temperature interface required by all reaction-network solvers.
    ARCH_INLINE double get_temperature(double rho, double e, const double *Xi) const
    {
        // Ideal-gas relation e=Cv*T, hence T=e/Cv.
        double cv_mix = get_mixture_Cv(Xi);
        if (cv_mix < 1e-12)
            return 0.0;
        return e / cv_mix;
    }

    ARCH_INLINE double get_eint_from_T(double rho, double T, const double *Xi) const
    {
        // Analytic ideal-gas relation e=Cv*T.
        double cv_mix = get_mixture_Cv(Xi);
        return cv_mix * T;
    }

    ARCH_INLINE double get_cv(double rho, double T, const double *Xi) const
    {
        // Cv is composition-dependent but temperature-independent for an ideal gas.
        return get_mixture_Cv(Xi);
    }

    template <int Equations>
    ARCH_INLINE void get_cv_gradient(double, double, const double*, double* gradient) const
    {
        // The same immutable coefficients as get_mixture_Cv; no EOS sampling
        // is needed for this linear mixture, even for large reaction networks.
        for (int i = 0; i < Equations - 1; ++i)
            gradient[i] = i < species.count ? species.get_Cv_ref(i) : 0.0;
        gradient[Equations - 1] = 0.0;
    }

    ARCH_INLINE double get_eta(double rho, double T, const double* Xi) const { return 0.0; }

    template <int Equations>
    ARCH_INLINE void get_energy_composition_gradient(
        double, double T, const double*, double* gradient) const
    {
        for (int i = 0; i < Equations - 1; ++i)
            gradient[i] = i < species.count ? T * species.get_Cv_ref(i) : 0.0;
    }

    template <int Equations>
    ARCH_INLINE void get_energy_composition_hessian_action(
        double, double, const double*, const double* flow, double* action) const
    {
        action[Equations - 1] = 0.0;
        for (int i = 0; i < Equations - 1; ++i) {
            action[i] = 0.0;
            if (i < species.count) action[Equations - 1] += species.get_Cv_ref(i) * flow[i];
        }
    }

    ARCH_INLINE double get_pressure(const FluidVector &U, const double *Xi) const
    {
        if (U.rho < 1e-12)
            return 0.0;
        double e_int = eos_utils::extract_specific_internal_energy(U);
        return std::max(0.0, get_pressure_from_rho_e(U.rho, e_int, Xi));
    }

    ARCH_INLINE double get_sound_speed(const FluidVector &U, double p, const double *Xi) const
    {
        if (U.rho < 1e-12)
            return 0.0;
        return std::sqrt(get_gamma(Xi) * p / U.rho);
    }

    ARCH_INLINE double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Xi) const
    {
        double gamma_mix = get_gamma(Xi);
        double e_internal_vol = p / (gamma_mix - 1.0);
        return e_internal_vol + eos_utils::calc_kinetic_energy(rho, u, v, w);
    }

    // Analytic derivatives used by implicit Jacobians.

    ARCH_INLINE double get_dp_drho_e(double rho, double e, const double *Xi) const
    {
        return (get_gamma(Xi) - 1.0) * e;
    }

    ARCH_INLINE double get_dp_de_rho(double rho, double e, const double *Xi) const
    {
        return (get_gamma(Xi) - 1.0) * rho;
    }

    ARCH_INLINE void evaluate_state(eos_state_t& state) const {
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

struct IdealGasHostView : IdealGasView
{
    const SpeciesManager *host_species = nullptr;
    const SpeciesManager *get_species_manager() const { return host_species; }
};

struct IdealGas : public EOSBase
{
    const SpeciesManager manager;
    double global_gamma;

private:
    std::vector<double> cached_gamma;
    std::vector<double> cached_Cv;

    void cache_species_thermodynamics()
    {
        cached_gamma.reserve(manager.species_list.size());
        cached_Cv.reserve(manager.species_list.size());
        for (const GasProperty &value : manager.species_list) {
            cached_gamma.push_back(value.gamma_ref);
            cached_Cv.push_back(value.Cv_ref);
        }
    }

public:
    IdealGas(const SpeciesManager &m) : manager(m), global_gamma(1.4)
    {
        cache_species_thermodynamics();
    }

    IdealGas(double default_gamma, const SpeciesManager &m)
        : manager(m), global_gamma(default_gamma)
    {
        cache_species_thermodynamics();
    }

    const SpeciesManager *get_species_manager() const { return &manager; }

    IdealGasHostView get_view() const
    {
        IdealGasHostView view{};
        view.species = {
            nullptr,
            nullptr,
            cached_gamma.empty() ? nullptr : cached_gamma.data(),
            cached_Cv.empty() ? nullptr : cached_Cv.data(),
            manager.count()};
        view.global_gamma = global_gamma;
        view.host_species = &manager;
        return view;
    }

    double get_gamma(const double *Xi) const { return get_view().get_gamma(Xi); }
    double get_mixture_Cv(const double *Xi) const { return get_view().get_mixture_Cv(Xi); }
    double get_pressure_from_rho_T(double rho, double T, const double *Xi) const
    { return get_view().get_pressure_from_rho_T(rho, T, Xi); }
    double get_pressure_from_rho_e(double rho, double e, const double *Xi) const
    { return get_view().get_pressure_from_rho_e(rho, e, Xi); }
    double get_temperature(double rho, double e, const double *Xi) const
    { return get_view().get_temperature(rho, e, Xi); }
    double get_eint_from_T(double rho, double T, const double *Xi) const
    { return get_view().get_eint_from_T(rho, T, Xi); }
    double get_cv(double rho, double T, const double *Xi) const
    { return get_view().get_cv(rho, T, Xi); }

    template <int Equations>
    void get_cv_gradient(double rho, double T, const double* Xi, double* gradient) const
    { get_view().template get_cv_gradient<Equations>(rho, T, Xi, gradient); }

    template <int Equations>
    void get_energy_composition_gradient(double rho, double T, const double* Xi, double* gradient) const
    { get_view().template get_energy_composition_gradient<Equations>(rho, T, Xi, gradient); }

    template <int Equations>
    void get_energy_composition_hessian_action(
        double rho, double T, const double* Xi, const double* flow, double* action) const
    { get_view().template get_energy_composition_hessian_action<Equations>(rho, T, Xi, flow, action); }
    double get_eta(double rho, double T, const double *Xi) const
    { return get_view().get_eta(rho, T, Xi); }
    double get_pressure(const FluidVector &U, const double *Xi) const
    { return get_view().get_pressure(U, Xi); }
    double get_sound_speed(const FluidVector &U, double p, const double *Xi) const
    { return get_view().get_sound_speed(U, p, Xi); }
    double get_total_energy_primitive(double rho, double u, double v, double w,
                                      double p, const double *Xi) const
    { return get_view().get_total_energy_primitive(rho, u, v, w, p, Xi); }
    double get_dp_drho_e(double rho, double e, const double *Xi) const
    { return get_view().get_dp_drho_e(rho, e, Xi); }
    double get_dp_de_rho(double rho, double e, const double *Xi) const
    { return get_view().get_dp_de_rho(rho, e, Xi); }
    void evaluate_state(eos_state_t &state) const { get_view().evaluate_state(state); }
};
