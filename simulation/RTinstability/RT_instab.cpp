/**
 * @file RT_instab.cpp
 * @brief User-defined Problem: 2D/3D Rayleigh-Taylor Instability.
 *        Standard benchmark from Liska & Wendroff (2003).
 */

/**
 * Workflow:
 * 1. Read the selected runtime mode and problem parameters.
 * 2. Construct physically consistent cell states and refinement indicators.
 * 3. Hand the initialized problem to the common AMR driver without solver-specific shortcuts.
 */

#include <cmath>
#include "physics/constant/PhysicalConstants.h"
#include <iostream>
#include <vector>

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"

class RTInstability
{
    // Fluid Properties (Density and Pressure stratification)
    double g_rho_heavy, g_rho_light;
    double g_y_int; // Interface y-coordinate
    double g_P_int; // Pressure at the interface

    // Perturbation Control (Triggering the instability)
    double g_amp; // Velocity perturbation amplitude
    double g_Lx;  // Domain length in X
    double g_Lz;  // Domain length in Z (for 3D)
    bool g_is_3d;

    // Physics Environment
    double g_gy; // Global gravity acceleration

    // Species Tracking (Used to visualize fluid mixing)
    int g_sp_heavy, g_sp_light;

public:
    void Setup(SimConfig &config, SpeciesManager &specs)
    {
        // 1. Grid bounds for wave number calculation
        g_Lx = config.grid.x1_max - config.grid.x1_min;
        g_is_3d = config.grid.dim == 3;
        g_Lz = g_is_3d ? config.grid.x3_max - config.grid.x3_min : 0.0;

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
        g_sp_light = specs.add_species("LightFluid", 1.0, 1.0, config.physics.gamma, 717.5);
        g_sp_heavy = specs.add_species("HeavyFluid", 4.0, 2.0, config.physics.gamma, 717.5);

        std::cout << "[Problem] Rayleigh-Taylor Instability Setup Complete.\n"
                  << "          Interface at y=" << g_y_int << "\n"
                  << "          Gravity g_y=" << g_gy << " (Matched with PhysicsConfig)\n"
                  << "          Mode: " << (g_is_3d ? "3D Box" : "2D Planar") << "\n";
    }

    void Init(const PointCoords &p, PrimitiveData &out) const
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
        // A cosine mode seeds the instability. The exponential envelope confines
        // the perturbation to the material interface; 15 sets its inverse width.
        double decay = std::exp(-15.0 * std::abs(p.y - g_y_int));
        double pert_x = std::cos(2.0 * arch::constants::math::pi * p.x / g_Lx);

        if (g_is_3d)
        {
            double pert_z = std::cos(2.0 * arch::constants::math::pi * p.z / g_Lz);
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
};

REGISTER_PROBLEM_CLASS("RT", RTInstability);
