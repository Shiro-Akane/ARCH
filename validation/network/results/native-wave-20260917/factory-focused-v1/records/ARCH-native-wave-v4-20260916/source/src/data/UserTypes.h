/**
 * @file UserTypes.h
 * @brief User-defined data structures and callback function signatures.
 */

#pragma once

#include <vector>

#include "GlobalDefs.h"

// Forward declaration to avoid circular dependency
struct SpeciesManager;

/**
 * @struct PrimitiveData
 * @brief Represents fluid state in Primitive Variables (Physical Observables).
 * Unlike conserved variables (density, momentum, energy), primitive variables
 * are \f$ W = (\rho, u, v, w, p) \f$. Problem callbacks supply these values
 * to the common EOS-backed initialization adapter.
 */
struct PrimitiveData
{
    double rho = 0.0; ///< Mass density (\f$ \rho \f$).
    double u = 0.0;   ///< First native orthonormal velocity component.
    double v = 0.0;   ///< Second native orthonormal velocity component.
    double w = 0.0;   ///< Third native orthonormal velocity component.
    double p = 0.0;   ///< Thermal pressure (\f$ P \f$).
    double temperature = 0.0; ///< Optional EOS temperature used when has_temperature is true.
    bool has_temperature = false; ///< Selects temperature as the EOS initialization input.

    std::vector<double> mass_fractions; ///< Mass fractions of chemical species (\f$ X_i \f$).

    /**
     * @brief Helper to set mass fraction for a specific species ID.
     * @warning May resize storage; call only during problem initialization, not
     * from time-stepping kernels.
     */

    void SetMassFraction(int id, double val)
    {
        if (id >= mass_fractions.size())
            mass_fractions.resize(id + 1, 0.0);
        mass_fractions[id] = val;
    }

    void SetTemperature(double val)
    {
        temperature = val;
        has_temperature = true;
    }
};

/**
 * @brief Function pointer type for simulation setup.
 * Used to configure global parameters or species properties before grid allocation.
 */

using SetupFunc = void (*)(SimConfig &, SpeciesManager &);

/**
 * @brief Function pointer type for Initial Condition (IC) generation.
 * Defines the spatial distribution of primitive variables at t=0.
 * Mapping: \f$ (x, y, z) \to (\rho, \vec{v}, P, X_i) \f$.
 */
using InitFunc = void (*)(const PointCoords &p, PrimitiveData &);
