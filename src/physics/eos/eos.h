/**
 * @file EOS.h
 * @brief Unified concept/interface for Equation of State.
 * Note: We do NOT use 'virtual' here to guarantee 100% inlining in tight CFD loops.
 * All EOS implementations must provide the following signatures.
 */
#pragma once

#include <cmath>
#include <vector>
#include "../species/Species.h"
#include "../../data/FluidState.h"
#include "eos_state.h"

// 仅仅是一个空基类，用于标记这是一个 EOS 策略
struct EOSBase
{
};

/* * ====================================================================
 * EXPECTED EOS INTERFACE (Duck Typing for Templates)
 * ====================================================================
 * Any concrete EOS (Ideal, Tabular, etc.) must implement:
 * * double get_gamma(const double *Xi) const;
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
 * ====================================================================
 */