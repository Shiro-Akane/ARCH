/**
 * @file BurnGradient.cpp
 * @brief Initialize a spatial burn pulse for ENUC-driven AMR and restart checks.
 *
 * Setup validates the one-dimensional burn case and resolves composition
 * through the common network registry. Init supplies a Gaussian temperature
 * profile at uniform density; the common EOS adapter constructs energy and the
 * production driver performs burn evolution, refinement and checkpointing.
 */

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"

class BurnGradientProblem
{
    double density_ = 1.0e7;
    double background_temperature_ = 5.0e7;
    double peak_temperature_ = 3.0e9;
    double center_ = 0.5;
    double width_ = 0.08;
    std::vector<double> mass_fractions_;

public:
    void Setup(SimConfig& config, SpeciesManager& species)
    {
        if (config.grid.dim != 1 || config.grid.geometry != "cartesian")
            throw std::invalid_argument(
                "BurnGradient requires one-dimensional Cartesian geometry.");
        if (!config.physics.burn.use_burn)
            throw std::invalid_argument("BurnGradient requires use_burn=true.");

        density_ = config.Get<double>("rho0", density_);
        background_temperature_ = config.Get<double>(
            "background_temperature", background_temperature_);
        peak_temperature_ = config.Get<double>(
            "peak_temperature", peak_temperature_);
        center_ = config.Get<double>("center_x", center_);
        width_ = config.Get<double>("width", width_);
        if (density_ <= 0.0 || background_temperature_ <= 0.0
            || peak_temperature_ < background_temperature_ || width_ <= 0.0)
            throw std::invalid_argument(
                "BurnGradient requires positive ordered thermodynamic inputs.");

        ProblemHelper::SetupNetworkAndFractions(
            config, species, mass_fractions_);
        std::cout << "[Problem] Burn gradient: rho=" << density_
                  << ", T_bg=" << background_temperature_
                  << ", T_peak=" << peak_temperature_
                  << ", network=" << config.physics.burn.network_name
                  << std::endl;
    }

    void Init(const PointCoords& point, PrimitiveData& state) const
    {
        const double offset = (point.x - center_) / width_;
        const double temperature = background_temperature_
            + (peak_temperature_ - background_temperature_)
                * std::exp(-offset * offset);
        state.rho = density_;
        state.p = 0.0;
        state.SetTemperature(temperature);
        state.u = 0.0;
        state.v = 0.0;
        state.w = 0.0;
        state.mass_fractions = mass_fractions_;
    }
};

REGISTER_PROBLEM_CLASS("BurnGradient", BurnGradientProblem);
