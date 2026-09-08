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

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"

class ExternalGravityProblem
{
    double density_ = 1.0;
    double pressure_ = 1.0;
    double velocity_x_ = 0.0;

public:
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
