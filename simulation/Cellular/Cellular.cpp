/**
 * @file Cellular.cpp
 * @brief User-defined Problem: Cellular Detonation with Helmholtz EOS & Pynucastro Networks.
 */

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"

#include <cmath>
#include <iostream>
#include <vector>
#include <stdexcept>

class CellularDetonation
{
    // ========================================================================
    // Global State for the Problem (Populated once in Setup, used in Init)
    // ========================================================================
    double rho_amb, p_amb, T_amb;
    double rho_vn, p_vn, T_vn, u_vn;

    // Geometry & Perturbation
    double distance;
    double noise_amp;
    int shock_dir;

    // Species & Pre-calculated Mass Fractions
    std::vector<double> default_X;

public:
    void Setup(SimConfig &config, SpeciesManager &specs)
    {
        // [1. Read Thermodynamics & Kinematics (Temperature-based)]
        rho_amb = config.Get<double>("rhoAmbient", 1.0e7);
        T_amb = config.Get<double>("tempAmbient", 2.0e8);

        rho_vn = config.Get<double>("rhoPerturb", 4.0e7);
        T_vn = config.Get<double>("tempPerturb", 3.0e9);
        u_vn = config.Get<double>("velxPerturb", 1.0e9);

        // [2. Read Geometry Configuration]
        distance = config.Get<double>("radiusPerturb", 0.5);
        noise_amp = config.Get<double>("noiseAmplitude", 0.0);
        shock_dir = config.Get<int>("shock_dir", 0);

        // [3. Setup Species & Mass Fractions]
        ProblemHelper::SetupNetworkAndFractions(config, specs, default_X);

        // [4. Thermodynamically Consistent Pressure Calculation]
        p_amb = ProblemHelper::GetPressureFromRhoT(config, specs, rho_amb, T_amb, default_X.data());
        p_vn = ProblemHelper::GetPressureFromRhoT(config, specs, rho_vn, T_vn, default_X.data());

        // [5. Console Output]
        std::cout << "[Problem] Cellular Detonation Setup Complete.\n"
                  << "          Shock Dir  : " << shock_dir << " (0=X, 1=Y, 2=Z)\n"
                  << "          Spike Dist : " << distance << "\n"
                  << "          Amb Press  : " << p_amb << " erg/cm^3\n"
                  << "          VN Press   : " << p_vn << " erg/cm^3\n";
    }

    void Init(const PointCoords &p, PrimitiveData &out) const
    {
        // [1. Coordinate & Perturbation Calculation]
        double r = p.x;
        if (shock_dir == 1)
            r = p.y;
        if (shock_dir == 2)
            r = p.z;

        double noise = 0.0;
        if (noise_amp > 0.0)
        {
            // Use a macroscopic transverse perturbation to avoid numerical dissipation.
            // Assumes a domain transverse length of ~25.6 to fit 4 wavelengths.
            double k_trans = 2.0 * M_PI / (25.6 / 4.0);
            
            if (shock_dir == 0) {
                noise = noise_amp * std::sin(k_trans * p.y) * std::cos(k_trans * p.z);
            } else if (shock_dir == 1) {
                noise = noise_amp * std::sin(k_trans * p.x) * std::cos(k_trans * p.z);
            } else {
                noise = noise_amp * std::sin(k_trans * p.x) * std::cos(k_trans * p.y);
            }
        }

        // [2. Assign Hydrodynamic State]
        if (r < distance)
        {
            out.rho = rho_vn * (1.0 + noise);
            out.p = p_vn * (1.0 + noise * 1.5);
            out.u = (shock_dir == 0) ? u_vn : 0.0;
            out.v = (shock_dir == 1) ? u_vn : 0.0;
            out.w = (shock_dir == 2) ? u_vn : 0.0;
        }
        else
        {
            out.rho = rho_amb;
            out.p = p_amb;
            out.u = 0.0;
            out.v = 0.0;
            out.w = 0.0;
        }

        if (p.x < 1e-5)
            // std::cout << "Init checking cell 0..." << std::endl;

        // [3. Apply Pre-calculated Mass Fractions]
        out.mass_fractions = default_X;
    }
};

// Registration Macro
REGISTER_PROBLEM_CLASS("CellularDet", CellularDetonation);