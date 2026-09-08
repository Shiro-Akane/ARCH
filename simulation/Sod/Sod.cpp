/**
 * @file Sod.cpp
 * @brief Initialize the one-dimensional Cartesian Sod shock tube.
 *
 * Setup validates left/right primitive states and an interior discontinuity.
 * Init chooses the state at each cell location; the common problem adapter
 * performs EOS conversion and the selected hydro driver evolves the Riemann data.
 */

#include <iostream>
#include <stdexcept>

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"

class SodProblem
{
    double interface_x_ = 0.5;
    double rho_left_ = 1.0;
    double pressure_left_ = 1.0;
    double velocity_left_ = 0.0;
    double rho_right_ = 0.125;
    double pressure_right_ = 0.1;
    double velocity_right_ = 0.0;
    int gas_id_ = -1;

public:
    void Setup(SimConfig &config, SpeciesManager &specs)
    {
        if (config.grid.geometry != "cartesian" || config.grid.dim != 1) {
            throw std::invalid_argument(
                "Sod benchmark requires a one-dimensional Cartesian grid "
                "(nblockx2 = nblockx3 = 0).");
        }

        interface_x_ = config.Get<double>("x_pos", 0.5);
        rho_left_ = config.Get<double>("rho_left", 1.0);
        pressure_left_ = config.Get<double>("p_left", 1.0);
        velocity_left_ = config.Get<double>("u_left", 0.0);
        rho_right_ = config.Get<double>("rho_right", 0.125);
        pressure_right_ = config.Get<double>("p_right", 0.1);
        velocity_right_ = config.Get<double>("u_right", 0.0);

        if (interface_x_ <= config.grid.x1_min || interface_x_ >= config.grid.x1_max ||
            rho_left_ <= 0.0 || rho_right_ <= 0.0 ||
            pressure_left_ <= 0.0 || pressure_right_ <= 0.0) {
            throw std::invalid_argument(
                "Sod requires an interior interface and positive densities/pressures.");
        }

        gas_id_ = specs.add_species(
            "SodGas", 1.0, 1.0, config.physics.gamma, 1.0);

        std::cout << "[Problem] Sod shock tube: x0=" << interface_x_
                  << ", left=(" << rho_left_ << ", " << pressure_left_
                  << "), right=(" << rho_right_ << ", "
                  << pressure_right_ << ")\n";
    }

    void Init(const PointCoords &point, PrimitiveData &out) const
    {
        const bool left = point.x < interface_x_;
        out.rho = left ? rho_left_ : rho_right_;
        out.p = left ? pressure_left_ : pressure_right_;
        out.u = left ? velocity_left_ : velocity_right_;
        out.v = 0.0;
        out.w = 0.0;
        out.SetMassFraction(gas_id_, 1.0);
    }
};

REGISTER_PROBLEM_CLASS("Sod", SodProblem);
