/**
 * @file Gaussian.cpp
 * @brief User-defined Problem: Pure Diffusion of a Gaussian Pulse.
 *
 * Workflow:
 * 1. Setup(): Read pulse parameters (amplitude, width, location) from configuration.
 * 2. Setup(): Register dummy species for species-diffusion verification.
 * 3. Init(): Set uniform hydrodynamic state (density, pressure) and 0 velocity.
 * 4. Init(): Set the Gaussian profile in species mass fraction.
 */

#include <cmath>
#include <vector>

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"

// Gaussian species-pulse benchmark.

class GaussianPulse
{
    double m_density;
    double m_pressure;
    double m_amp;
    double m_width;
    double m_xc;
    int m_bg_id;
    int m_ps_id;
    std::vector<double> default_X;

public:
    // 1. Setup Phase (Parameter Extraction)
    void Setup(SimConfig &config, SpeciesManager &specs)
    {
        m_density = config.Get<double>("rho0", 1.0);
        m_pressure = config.Get<double>("p0", 1.0);
        m_amp = config.Get<double>("amp", 0.5);
        m_width = config.Get<double>("width", 0.1);
        m_xc = config.Get<double>("xc", 0.5);
        ProblemHelper::SetupNetworkAndFractions(config, specs, default_X);

        if (specs.count() >= 2) {
            m_bg_id = 0;
            m_ps_id = 1;
        } else {
            m_bg_id = specs.add_species("BgGas", 1.0, 1.0, 1.4, 717.5);
            m_ps_id = specs.add_species("PassiveGas", 1.0, 1.0, 1.4, 717.5);
        }

        default_X.assign(specs.count(), 0.0);
    }

    // 2. Initialization Phase (Set Initial Conditions)
    void Init(const PointCoords &p, PrimitiveData &out) const
    {
        // 1. Uniform Hydrodynamic state (no pressure gradients)
        out.rho = m_density;
        out.p   = m_pressure;
        out.u   = 0.0;
        out.v   = 0.0;
        out.w   = 0.0;

        // 2. Gaussian profile for the passive scalar
        double r = p.r;
        double theta = p.phi; // In 2D spherical falling back to polar, azimuthal angle is p.phi

        // Spherical distance squared from center (xc, theta=0)
        double dist2 = r*r + m_xc*m_xc - 2.0*r*m_xc*std::cos(theta);

        double x_passive = m_amp * std::exp(-dist2 / (m_width * m_width));

        // Ensure bounds
        if (x_passive > 1.0) x_passive = 1.0;
        if (x_passive < 0.0) x_passive = 0.0;

        // 3. Set Mass Fractions
        for (size_t i = 0; i < default_X.size(); ++i) {
            out.SetMassFraction(i, default_X[i]);
        }
        out.SetMassFraction(m_ps_id, x_passive);
        out.SetMassFraction(m_bg_id, 1.0 - x_passive);
    }
};

REGISTER_PROBLEM_CLASS("Gaussian", GaussianPulse);
