/**
 * @file BurnOneZone.cpp
 * @brief Uniform one-zone network integration through the production burn driver.
 */

#include <iostream>
#include <stdexcept>
#include <vector>

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"

class BurnOneZoneProblem
{
    double density_ = 1.0e7;
    double temperature_ = 3.0e9;
    double pressure_ = 0.0;
    std::vector<double> mass_fractions_;

public:
    void Setup(SimConfig& config, SpeciesManager& species)
    {
        if (config.grid.dim != 1 || config.grid.geometry != "cartesian") {
            throw std::invalid_argument("BurnOneZone requires one-dimensional Cartesian geometry.");
        }
        if (!config.physics.burn.use_burn) {
            throw std::invalid_argument("BurnOneZone requires use_burn=true.");
        }

        density_ = config.Get<double>("rho0", 1.0e7);
        temperature_ = config.Get<double>("temperature0", 3.0e9);
        ProblemHelper::SetupNetworkAndFractions(config, species, mass_fractions_);
        pressure_ = ProblemHelper::GetPressureFromRhoT(
            config, species, density_, temperature_, mass_fractions_.data());

        std::cout << "[Problem] Burn one-zone state: rho=" << density_
                  << ", T=" << temperature_ << ", p=" << pressure_
                  << ", network=" << config.physics.burn.network_name << std::endl;
    }

    void Init(const PointCoords&, PrimitiveData& state) const
    {
        state.rho = density_;
        state.p = pressure_;
        state.SetTemperature(temperature_);
        state.u = 0.0;
        state.v = 0.0;
        state.w = 0.0;
        state.mass_fractions = mass_fractions_;
    }
};

REGISTER_PROBLEM_CLASS("BurnOneZone", BurnOneZoneProblem);
