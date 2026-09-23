/**
 * @file SNIa2DCoupled.cpp
 * @brief Two-dimensional C/O ignition patch exercising four production modules.
 *
 * Workflow:
 * 1. Setup checks a Cartesian periodic self-gravity, Helmholtz EOS, aprox13
 *    network, active burn and thermal diffusion configuration.
 * 2. Init supplies a smooth, finite C/O density and temperature hotspot.
 * 3. The normal Driver performs hydro, Poisson self-gravity, burning and
 *    diffusion. The case adds no private numerical implementation.
 *
 * This is a centimetre-scale coupled execution example inspired by the C/O
 * fuel in FLASH 4.8's RTFlame example. Periodic 2D gravity represents a
 * translation-invariant Poisson model, not an isolated 3D white dwarf.
 */

#include <cmath>
#include <stdexcept>
#include <vector>

// Stable case API; GlobalDefs also exports the shared CGS constants.
#include <UserInterface.h>
#include <GlobalDefs.h>

class SNIa2DCoupledProblem
{
    double density_ = 1.0e7;
    double background_temperature_ = 1.0e9;
    double peak_temperature_ = 3.0e9;
    double density_amplitude_ = 0.01;
    double width_ = 0.15;
    double center_x_ = 0.5;
    double center_y_ = 0.5;
    std::vector<double> fractions_;

public:
    /** Validate the four-module contract and load the network composition. */
    void Setup(SimConfig& config, SpeciesManager& species)
    {
        if (config.grid.dim != 2 || config.grid.geometry != "cartesian"
            || config.physics.gravity.type != "self"
            || config.physics.gravity.boundary != "periodic"
            || config.physics.eos_type != "helmholtz"
            || !config.physics.burn.use_burn
            || config.physics.burn.network_name != "aprox13"
            || !config.physics.diffusion.use_diffusion
            || !config.physics.diffusion.use_thermal_diffusion)
            throw std::invalid_argument(
                "SNIa2DCoupled requires periodic Cartesian 2D, self gravity, "
                "Helmholtz EOS, aprox13 burning and thermal diffusion");
        for (const auto* boundary : {
                 &config.grid.x1l_boundary_type, &config.grid.x1r_boundary_type,
                 &config.grid.x2l_boundary_type, &config.grid.x2r_boundary_type})
            if (*boundary != "periodic")
                throw std::invalid_argument("SNIa2DCoupled requires periodic fluid faces");

        density_ = config.Get<double>("rho0", density_);
        background_temperature_ = config.Get<double>("temperature0", background_temperature_);
        peak_temperature_ = config.Get<double>("temperature_peak", peak_temperature_);
        density_amplitude_ = config.Get<double>("density_amplitude", density_amplitude_);
        width_ = config.Get<double>("hotspot_width", width_);
        center_x_ = config.Get<double>("center_x", center_x_);
        center_y_ = config.Get<double>("center_y", center_y_);
        if (!std::isfinite(density_) || !std::isfinite(background_temperature_)
            || !std::isfinite(peak_temperature_) || !std::isfinite(density_amplitude_)
            || !std::isfinite(width_) || !std::isfinite(center_x_) || !std::isfinite(center_y_)
            || density_ <= 0.0 || background_temperature_ <= 0.0
            || peak_temperature_ < background_temperature_
            || density_amplitude_ < 0.0 || width_ <= 0.0)
            throw std::invalid_argument("SNIa2DCoupled requires finite positive hotspot inputs");

        ProblemHelper::SetupNetworkAndFractions(config, species, fractions_);
    }

    /** Supply smooth primitive fields; the shared EOS converts T and X to e. */
    void Init(const PointCoords& point, PrimitiveData& state) const
    {
        const double dx = (point.x - center_x_) / width_;
        const double dy = (point.y - center_y_) / width_;
        // q = exp[-((x-x_c)^2+(y-y_c)^2)/(2 sigma^2)].
        const double hotspot = std::exp(-0.5 * (dx * dx + dy * dy));
        // rho = rho_0 (1 + A q), T = T_0 + (T_peak-T_0) q.
        state.rho = density_ * (1.0 + density_amplitude_ * hotspot);
        state.p = 0.0;
        state.SetTemperature(background_temperature_
                             + (peak_temperature_ - background_temperature_) * hotspot);
        state.u = 0.0;
        state.v = 0.0;
        state.w = 0.0;
        state.mass_fractions = fractions_;
    }
};

REGISTER_PROBLEM_CLASS("SNIa2DCoupled", SNIa2DCoupledProblem);
