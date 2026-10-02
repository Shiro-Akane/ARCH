/**
 * @file ExternalGravity.cpp
 * @brief Initialize a uniform state for constant external acceleration.
 *
 * Setup requires the one-dimensional Cartesian external-gravity case. Init
 * supplies density, pressure and initial velocity; the common gravity source
 * supplies the momentum and energy updates during production evolution.
 */

#include <iostream>
#include <stdexcept>

#include <UserInterface.h>
#include <GlobalDefs.h>

class ExternalGravityProblem
{
    double density_ = 1.0;
    double pressure_ = 1.0;
    double velocity_x_ = 0.0;

public:
    static arch::config::CaseConfiguration DescribeConfiguration(
        const arch::config::StandardInputResolution&)
    {
        arch::config::CaseConfiguration result;
        result.complete = true;
        // This model does not initialize a network or consume network floors.
        result.consumers.needs_network = false;
        result.consumers.needs_temperature_floor = false;
        result.consumers.needs_composition_floor = false;
        result.parameters = {
            {"rho0", "float", "g/cm^3", "verification"}, {"pressure0", "float", "erg/cm^3", "verification"},
            {"velocity_x0", "float", "cm/s", "verification"}};
        return result;
    }

    void Setup(SimConfig& config, SpeciesManager&)
    {
        if (config.grid.dim != 1 || config.grid.geometry != "cartesian") {
            throw std::invalid_argument("ExternalGravity verification requires one-dimensional Cartesian geometry.");
        }
        if (config.physics.gravity.type != "external") {
            throw std::invalid_argument("ExternalGravity verification requires gravity_type=external.");
        }

        density_ = config.Get<double>("rho0", 1.0);
        pressure_ = config.Get<double>("pressure0", 1.0);
        velocity_x_ = config.Get<double>("velocity_x0", 0.0);

        std::cout << "[Problem] Uniform external-gravity state: rho=" << density_
                  << ", p=" << pressure_ << ", u0=" << velocity_x_
                  << ", gx=" << config.physics.gravity.g_x << std::endl;
    }

    void Init(const PointCoords&, PrimitiveData& state) const
    {
        state.rho = density_;
        state.p = pressure_;
        state.u = velocity_x_;
        state.v = 0.0;
        state.w = 0.0;
    }
};

REGISTER_PROBLEM_CLASS("ExternalGravity", ExternalGravityProblem);
