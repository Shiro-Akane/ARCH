/**
 * @file EOS.h
 * @brief Unified concept/interface for Equation of State.
 * EOS implementations use a compile-time policy interface so tight CFD loops
 * can inline thermodynamic calls. Each policy provides the signatures below.
 */
#pragma once

#include <cmath>
#include <vector>

#include "eos_state.h"

#include "../../data/FluidState.h"
#include "../species/Species.h"

// Canonical view declarations for runtime ABIs. Declaring a function that
// accepts a view does not require any interpolation or thermodynamic body.
// Concrete callers/owners include the selected EOS implementation directly.
struct IdealGasView;
template <class SpeciesView> struct BasicHelmEosView;
template <class SpeciesView> struct BasicTabular3DEOSView;
template <class SpeciesView> struct BasicTabular4DEOSView;
using HelmEosView = BasicHelmEosView<SpeciesPODView>;
using Tabular3DEOSView = BasicTabular3DEOSView<SpeciesPODView>;
using Tabular4DEOSView = BasicTabular4DEOSView<SpeciesPODView>;

// Empty marker base for equation-of-state policies.
struct EOSBase
{
};

/**
 * Expected compile-time EOS policy interface.
 * Any concrete EOS (Ideal, Tabular, etc.) must implement:
 * double get_gamma(const double *Xi) const;
 * double get_eta(double rho, double T, const double *Xi) const;
 * double get_pressure(const FluidVector &U, const double *Xi) const;
 * double get_temperature(const FluidVector &U, const double *Xi) const;
 * double get_sound_speed(const FluidVector &U, double p, const double *Xi) const;
 * double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Xi) const;
 * double get_pressure_from_rho_e(double rho, double e, const double *Xi) const;
 * double get_pressure_from_rho_T(double rho, double T, const double *Xi) const;
 * double get_eint_from_T(double rho, double T_target, const double *Xi) const;
 * double get_cv(double rho, double T, const double *Xi) const;
 * double get_dp_drho_e(double rho, double e, const double *Xi) const;
 * double get_dp_de_rho(double rho, double e, const double *Xi) const;
 * void evaluate_state(eos_state_t& state) const;
 * const SpeciesManager* get_species_manager() const;
 *
 * evaluate_state must fill finite P, E, cv, sound_speed, dp_drho, and dp_dT
 * for every valid input state.  eos_utils::get_isentropic_state_at_pressure_factor
 * supplies the shared fixed-composition isentrope implementation for every
 * policy; concrete EOS types must not duplicate that solver.
 */
