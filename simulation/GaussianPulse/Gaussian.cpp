/**
 * @file Gaussian.cpp
 * @brief Gaussian species pulse with optional pressure and velocity perturbations.
 *
 * Workflow:
 * 1. Setup(): Read pulse parameters (amplitude, width, location) from configuration.
 * 2. Setup(): Use registered network species or add passive gas species.
 * 3. Init(): Evaluate one envelope in Grid's physical Cartesian coordinates.
 * 4. Init(): Apply it to species and optional hydrodynamic perturbations.
 */

#include <cmath>
#include <stdexcept>
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
    double m_yc;
    double m_zc;
    double m_pressure_amplitude;
    double m_u_amplitude, m_v_amplitude, m_w_amplitude;
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
        m_yc = config.Get<double>("yc", 0.0);
        m_zc = config.Get<double>("zc", 0.0);
        m_pressure_amplitude = config.Get<double>("pressure_amplitude", 0.0);
        m_u_amplitude = config.Get<double>("u_amplitude", 0.0);
        m_v_amplitude = config.Get<double>("v_amplitude", 0.0);
        m_w_amplitude = config.Get<double>("w_amplitude", 0.0);
        for (double value : {m_density, m_pressure, m_amp, m_width, m_xc, m_yc, m_zc,
                             m_pressure_amplitude, m_u_amplitude, m_v_amplitude, m_w_amplitude})
            if (!std::isfinite(value))
                throw std::invalid_argument("Gaussian parameters must be finite");
        if (m_density <= 0.0 || m_pressure <= 0.0 || m_width <= 0.0 ||
            m_pressure_amplitude <= -1.0)
            throw std::invalid_argument(
                "Gaussian requires positive rho0, p0, width and pressure_amplitude > -1");
        // A passive pulse has no reaction network by default. Explicit nuclear
        // network selections (e.g. with Helmholtz) use the common factory.
        config.physics.burn.network_name = config.Get<std::string>("network_name", "none");
        ProblemHelper::SetupNetworkAndFractions(config, specs, default_X);

        if (specs.count() >= 2) {
            m_bg_id = 0;
            m_ps_id = 1;
        } else {
            const double cv = config.Get<double>("gas_cv", 717.5);
            if (!std::isfinite(cv) || cv <= 0.0)
                throw std::invalid_argument("Gaussian gas_cv must be finite and positive");
            m_bg_id = specs.add_species("BgGas", 1.0, 1.0, config.physics.gamma, cv);
            m_ps_id = specs.add_species("PassiveGas", 1.0, 1.0, config.physics.gamma, cv);
        }

        default_X.assign(specs.count(), 0.0);
    }

    // 2. Initialization Phase (Set Initial Conditions)
    void Init(const PointCoords &p, PrimitiveData &out) const
    {
        // Grid already supplies the Cartesian position for every native geometry.
        // Scaling before squaring also avoids forming width^2 for narrow pulses.
        const double dx = (p.x - m_xc) / m_width;
        const double dy = (p.y - m_yc) / m_width;
        const double dz = (p.z - m_zc) / m_width;
        const double pulse = std::exp(-(dx*dx + dy*dy + dz*dz));

        // Pressure amplitude is fractional; velocities use native orthonormal
        // components in the problem's velocity units. Zero amplitudes leave the
        // uniform pressure and stationary hydrodynamic background.
        out.rho = m_density;
        out.p   = m_pressure * (1.0 + m_pressure_amplitude * pulse);
        out.u   = m_u_amplitude * pulse;
        out.v   = m_v_amplitude * pulse;
        out.w   = m_w_amplitude * pulse;

        // 2. Gaussian profile for the passive scalar
        double x_passive = m_amp * pulse;

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
