/**
 * @file UserTypes.h
 * @brief User-defined data structures and callback function signatures.
 */

#pragma once

#include <vector>

#include "../data/GlobalDefs.h"

// Forward declaration to avoid circular dependency
struct SpeciesManager;

/**
 * @struct PrimitiveData
 * @brief Represents fluid state in Primitive Variables (Physical Observables).
 * * Unlike conserved variables (density, momentum, energy), primitive variables
 * * are \f$ W = (\rho, u, v, w, p) \f$. Used for EOS calls, boundary conditions, and IO.
 */
struct PrimitiveData
{
    double rho; ///< Mass density (\f$ \rho \f$).
    double u;   ///< Velocity X-component (\f$ v_x \f$).
    double v;   ///< Velocity Y-component (\f$ v_y \f$).
    double w;   ///< Velocity Z-component (\f$ v_z \f$).
    double p;   ///< Thermal pressure (\f$ P \f$).

    std::vector<double> mass_fractions; ///< Mass fractions of chemical species (\f$ X_i \f$).

    /**
     * @brief Helper to set mass fraction for a specific species ID.
     * @warning Contains memory allocation (resize). Use ONLY during initialization phase.
     * Do NOT use inside the main simulation loop (inner kernels).
     */

    void SetMassFraction(int id, double val)
    {
        if (id >= mass_fractions.size())
            mass_fractions.resize(id + 1, 0.0);
        mass_fractions[id] = val;
    }
};

/**
 * @brief Function pointer type for simulation setup.
 * * Used to configure global parameters or species properties before grid allocation.
 */

using SetupFunc = void (*)(SimConfig &, SpeciesManager &);

/**
 * @brief Function pointer type for Initial Condition (IC) generation.
 * * Defines the spatial distribution of primitive variables at t=0.
 * * Mapping: \f$ (x, y, z) \to (\rho, \vec{v}, P, X_i) \f$.
 */
using InitFunc = void (*)(double x, double y, double z, PrimitiveData &);