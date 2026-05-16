/**
 * @file RT_instab.cpp
 * @brief User-defined Problem: 2D/3D Rayleigh-Taylor Instability.
 *        Standard benchmark from Liska & Wendroff (2003).
 */

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"
#include <cmath>
#include <iostream>

namespace
{
    // Fluid properties
    double g_rho_heavy, g_rho_light;
    double g_y_int; // Interface y-coordinate
    double g_P_int; // Pressure at the interface

    // Perturbation
    double g_amp; // Velocity perturbation amplitude
    double g_Lx;  // Domain length in X
    double g_Lz;  // Domain length in Z (for 3D)

    // Physics
    double g_gy; // Global gravity acceleration

    // Species tracking (to visualize the fluid mixing)
    int g_sp_heavy, g_sp_light;
}

void RT_Setup(SimConfig &config, SpeciesManager &specs)
{
    // 1. Grid bounds for wave number calculation
    g_Lx = config.grid.x_max - config.grid.x_min;
    g_Lz = config.grid.z_max - config.grid.z_min;

    // 2. Sync gravity with the global physics configuration!
    // This guarantees the Hydrostatic Equilibrium matches the solver's source terms.
    g_gy = config.physics.gravity.g_y;

    // 3. Read custom RT parameters
    g_rho_heavy = config.Get<double>("rho_heavy", 2.0);
    g_rho_light = config.Get<double>("rho_light", 1.0);
    g_y_int = config.Get<double>("y_int", 0.5);
    g_P_int = config.Get<double>("p_int", 2.5);
    g_amp = config.Get<double>("amplitude", 0.025);

    // 4. Setup pseudo-species to track the mixing interfaces
    // Using the global gamma (e.g., 1.4). CV is arbitrary for ideal gas tracking.
    g_sp_light = specs.add_species("LightFluid", config.physics.gamma, 717.5);
    g_sp_heavy = specs.add_species("HeavyFluid", config.physics.gamma, 717.5);

    std::cout << "[Problem] Rayleigh-Taylor Instability Setup Complete.\n"
              << "          Interface at y=" << g_y_int << "\n"
              << "          Gravity g_y=" << g_gy << " (Matched with PhysicsConfig)\n"
              << "          Mode: " << (config.grid.nz > 1 ? "3D Box" : "2D Planar") << "\n";
}

void RT_Init(const PointCoords &p, PrimitiveData &out)
{
    // 1. Identify region (Heavy fluid on top)
    bool is_heavy = (p.y > g_y_int);
    out.rho = is_heavy ? g_rho_heavy : g_rho_light;

    // 2. Hydrostatic Pressure Field: P(y) = P_interface + rho * g * (y - y_interface)
    // Critical: g_gy is negative (e.g., -0.1), so pressure properly decreases with height.
    out.p = g_P_int + out.rho * g_gy * (p.y - g_y_int);

    // 3. Initial Velocity (Static)
    out.u = 0.0;
    out.w = 0.0;

    // 4. Velocity Perturbation in Y
    // A cosine wave to trigger the instability mode.
    // We apply an exponential decay so it's localized purely at the interface.
    double decay = std::exp(-15.0 * std::abs(p.y - g_y_int));
    double pert_x = std::cos(2.0 * M_PI * p.x / g_Lx);

    if (g_Lz > 1e-8) // If 3D box
    {
        double pert_z = std::cos(2.0 * M_PI * p.z / g_Lz);
        out.v = g_amp * pert_x * pert_z * decay;
    }
    else // If 2D planar
    {
        out.v = g_amp * pert_x * decay;
    }

    // 5. Mass Fraction Allocation
    if (is_heavy)
    {
        out.SetMassFraction(g_sp_heavy, 1.0);
        out.SetMassFraction(g_sp_light, 0.0);
    }
    else
    {
        out.SetMassFraction(g_sp_heavy, 0.0);
        out.SetMassFraction(g_sp_light, 1.0);
    }
}

REGISTER_PROBLEM("RT", RT_Setup, RT_Init);