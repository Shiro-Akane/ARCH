/**
 * @file SmoothAdvection.cpp
 * @brief Initialize a cell-averaged entropy wave for hydro reconstruction checks.
 *
 * Setup computes the root-cell averaging factor for the density mode. Init
 * keeps pressure and velocity uniform; the configured boundary conditions and
 * common hydro policies determine the subsequent advection.
 */

#include <cmath>
#include "physics/constant/PhysicalConstants.h"
#include <iostream>
#include <stdexcept>

#include "../../src/core/UserInterface.h"

#include "../../src/data/GlobalDefs.h"

class SmoothAdvectionProblem
{
    double x_min_ = 0.0;
    double length_ = 1.0;
    double rho_mean_ = 1.0;
    double rho_amplitude_ = 0.2;
    double pressure_ = 1.0;
    double velocity_ = 1.0;
    double cell_average_factor_ = 1.0;
    int mode_ = 1;

public:
    void Setup(SimConfig& config, SpeciesManager&)
    {
        if (config.grid.dim != 1 || config.grid.geometry != "cartesian") {
            throw std::invalid_argument("SmoothAdvection requires one-dimensional Cartesian geometry.");
        }

        x_min_ = config.grid.x1_min;
        length_ = config.grid.x1_max - config.grid.x1_min;
        rho_mean_ = config.Get<double>("rho_mean", 1.0);
        rho_amplitude_ = config.Get<double>("rho_amplitude", 0.2);
        pressure_ = config.Get<double>("pressure0", 1.0);
        velocity_ = config.Get<double>("velocity0", 1.0);
        mode_ = config.Get<int>("mode", 1);

        if (length_ <= 0.0 || mode_ < 1 || rho_mean_ <= std::abs(rho_amplitude_)) {
            throw std::invalid_argument("SmoothAdvection parameters require L>0, mode>=1, and positive density.");
        }

        const double cell_width = ProblemHelper::GetRootCellWidth(config, 1);
        const double half_cell_phase = arch::constants::math::pi * mode_ * cell_width / length_;
        cell_average_factor_ = std::sin(half_cell_phase) / half_cell_phase;

        std::cout << "[Problem] Smooth entropy wave: rho=" << rho_mean_
                  << "+" << rho_amplitude_ << " sin(2*pi*" << mode_ << "*x/L)"
                  << ", u=" << velocity_ << ", p=" << pressure_ << std::endl;
    }

    void Init(const PointCoords& point, PrimitiveData& state) const
    {
        const double phase = 2.0 * arch::constants::math::pi * mode_ * (point.x - x_min_) / length_;
        state.rho = rho_mean_ + rho_amplitude_ * cell_average_factor_ * std::sin(phase);
        state.p = pressure_;
        state.u = velocity_;
        state.v = 0.0;
        state.w = 0.0;
    }
};

REGISTER_PROBLEM_CLASS("SmoothAdvection", SmoothAdvectionProblem);
