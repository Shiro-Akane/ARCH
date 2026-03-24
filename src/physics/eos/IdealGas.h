/**
 * @file IdealGas.h
 * @brief Equation of State (EOS) solver for an Ideal Gas.
 * * Handles the thermodynamic relations between state variables for a multi-species gas.
 * * Assumes a Calorically Perfect Gas (constant gamma for each species).
 */

#pragma once

#include <cmath>
#include <vector>

#include "../species/Species.h"

#include "../../data/FluidState.h"

struct IdealGas
{
    // The manager holds species properties (gamma, molar mass, etc.)
    const SpeciesManager manager;

    // Fallback gamma for single-species/simple problems (when manager is empty)
    double global_gamma;

    /**
     * @brief Constructor.
     * @param m SpeciesManager containing component properties.
     */
    IdealGas(const SpeciesManager &m) : manager(m), global_gamma(1.4) {}

    /**
     * @brief Constructor 2: Hybrid (Compatible with SolverDispatch)
     * * This fixes the "No matching constructor" error.
     * * It allows passing a global gamma (from Config) AND the species manager.
     */
    IdealGas(double default_gamma, const SpeciesManager &m)
        : manager(m), global_gamma(default_gamma) {}
    /**
     * @brief Computes the mixture's specific heat ratio (Gamma).
     * Based on the mass-fraction weighted average of internal energies.
     * * Formula:
     * 1 / (gamma_mix - 1) = Sum( Y_i / (gamma_i - 1) )
     * * @param Yi Array of mass fractions for each species.
     * @return The effective gamma for the mixture.
     */

    double get_gamma(const double *Yi) const
    {
        // Safety / Fallback:
        // If no species are registered (e.g., simple Sod test), use the global gamma.
        if (manager.count() == 0)
        {
            return global_gamma;
        }

        double sum_inv_gamma_minus_1 = 0.0;

        for (int k = 0; k < manager.count(); ++k)
        {
            // Optimization: Skip trace species to save cycles
            if (Yi[k] > 1e-12)
            {
                double gamma_i = manager.get_gamma(k);
                sum_inv_gamma_minus_1 += Yi[k] / (gamma_i - 1.0);
            }
        }
        // Safety: If mass fractions are all zero (vacuum) or math fails
        if (sum_inv_gamma_minus_1 < 1e-9)
            return global_gamma;

        // Invert back to get gamma_mix
        return 1.0 / sum_inv_gamma_minus_1 + 1.0;
    }

    /**
     * @brief Computes Pressure from Conservative Variables.
     * * Relation:
     * P = (gamma - 1) * (E_total - E_kinetic)
     * P = (gamma - 1) * (E - 0.5 * rho * u^2)
     * * @param rho Density
     * @param mom Momentum Density (rho * u)
     * @param eng Total Energy Density (Internal + Kinetic)
     * @param Yi  Mass Fractions
     * @return Pressure (Pa)
     */
    double get_pressure(const FluidVector &U, const double *Yi) const
    {
        double gamma_mix = get_gamma(Yi);

        // Prevent division by zero in vacuum
        if (U.rho < 1e-12)
            return 0.0;

        // kinetic energy = 0.5 * (rho * u)^2 / rho = 0.5 * mom^2 / rho
        double e_kinetic = 0.5 * (U.mom_x * U.mom_x + U.mom_y * U.mom_y + U.mom_z * U.mom_z) / U.rho;

        // internal energy density = total - kinetic
        double e_int = U.eng - e_kinetic;

        return std::max(0.0, (gamma_mix - 1.0) * e_int);
    }

    /**
     * @brief Computes Sound Speed.
     * * Formula: c = sqrt( gamma * P / rho )
     * * @param rho Density
     * @param p   Pressure
     * @param Yi  Mass Fractions
     * @return Speed of sound (m/s)
     */
    double get_sound_speed(const FluidVector &U, double p, const double *Yi) const
    {
        if (U.rho < 1e-12)
            return 0.0;
        return std::sqrt(get_gamma(Yi) * p / U.rho);
    }

    /**
     * @brief Computes Total Energy Density from Primitive Variables.
     * Used mainly during initialization.
     * * Formula: E = P / (gamma - 1) + 0.5 * rho * u^2
     * * @param rho Density
     * @param u   Velocity
     * @param p   Pressure
     * @param Yi  Mass Fractions
     * @return Total Energy Density (J/m^3)
     */
    double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Yi) const
    {
        double gamma_mix = get_gamma(Yi);

        // Internal Energy Density: e_int = P / (gamma - 1)
        double e_internal = p / (gamma_mix - 1.0);

        // Kinetic Energy Density: e_kin = 0.5 * rho * u^2
        double e_kinetic = 0.5 * rho * (u * u + v * v + w * w);

        return e_internal + e_kinetic;
    }

    /**
     * @brief Computes Pressure from (rho, e).
     * * Necessary for Roe-Glaister average state calculation.
     * * Relation: p = (gamma - 1) * rho * e
     * * @param rho Density
     * @param e   Specific Internal Energy (J/kg)
     * @param Yi  Mass Fractions
     */
    double get_pressure_from_rho_e(double rho, double e, const double *Yi) const
    {
        // 理想气体状态方程: p = rho * R * T = rho * (gamma-1) * e
        return (get_gamma(Yi) - 1.0) * rho * e;
    }

    /**
     * @brief Partial derivative: dp / drho | constant e
     * * Used for Roe-Glaister and Implicit Jacobians.
     * * For Ideal Gas: p = (gamma - 1) * rho * e
     * * dp/drho = (gamma - 1) * e
     */
    double get_dp_drho_e(double rho, double e, const double *Yi) const
    {
        return (get_gamma(Yi) - 1.0) * e;
    }

    /**
     * @brief Partial derivative: dp / de | constant rho
     * * Used for Roe-Glaister and Implicit Jacobians.
     * * For Ideal Gas: p = (gamma - 1) * rho * e
     * * dp/de = (gamma - 1) * rho
     */
    double get_dp_de_rho(double rho, double e, const double *Yi) const
    {
        return (get_gamma(Yi) - 1.0) * rho;
    }
};