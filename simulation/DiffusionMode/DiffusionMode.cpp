/**
 * @file DiffusionMode.cpp
 * @brief Initialize a cell-averaged cosine mode for species diffusion.
 *
 * Setup computes the root-cell averaging factor and registers two species
 * with identical thermodynamic properties. Init supplies complementary mass
 * fractions at uniform density and pressure; the configured diffusion policy
 * and boundary conditions determine their subsequent evolution.
 */

#include <cmath>
#include "physics/constant/PhysicalConstants.h"
#include <iostream>
#include <stdexcept>

#include "../../src/core/UserInterface.h"

#include "../../src/data/GlobalDefs.h"

class DiffusionModeProblem
{
    double x_min_ = 0.0;
    double length_ = 1.0;
    double density_ = 1.0;
    double pressure_ = 1.0;
    double mean_ = 0.5;
    double amplitude_ = 0.25;
    double cell_average_factor_ = 1.0;
    int mode_ = 1;
    int background_id_ = -1;
    int tracer_id_ = -1;

public:
    void Setup(SimConfig& config, SpeciesManager& species)
    {
        if (config.grid.dim != 1 || config.grid.geometry != "cartesian") {
            throw std::invalid_argument("DiffusionMode requires one-dimensional Cartesian geometry.");
        }

        x_min_ = config.grid.x1_min;
        length_ = config.grid.x1_max - config.grid.x1_min;
        density_ = config.Get<double>("rho0", 1.0);
        pressure_ = config.Get<double>("pressure0", 1.0);
        mean_ = config.Get<double>("tracer_mean", 0.5);
        amplitude_ = config.Get<double>("tracer_amplitude", 0.25);
        mode_ = config.Get<int>("mode", 1);

        if (length_ <= 0.0 || mode_ < 1 || mean_ - std::abs(amplitude_) < 0.0 ||
            mean_ + std::abs(amplitude_) > 1.0) {
            throw std::invalid_argument("DiffusionMode parameters require a bounded mass fraction in [0,1].");
        }

        const double cell_width = ProblemHelper::GetRootCellWidth(config, 1);
        const double half_cell_phase = arch::constants::math::pi * mode_ * cell_width / length_;
        cell_average_factor_ = std::sin(half_cell_phase) / half_cell_phase;

        background_id_ = species.add_species("background", 1.0, 1.0,
                                             config.physics.gamma, 717.5);
        tracer_id_ = species.add_species("tracer", 1.0, 1.0,
                                         config.physics.gamma, 717.5);

        std::cout << "[Problem] Diffusion cosine mode: X=" << mean_ << "+"
                  << amplitude_ << " cos(2*pi*" << mode_ << "*x/L)" << std::endl;
    }

    void Init(const PointCoords& point, PrimitiveData& state) const
    {
        const double phase = 2.0 * arch::constants::math::pi * mode_ * (point.x - x_min_) / length_;
        const double tracer = mean_ + amplitude_ * cell_average_factor_ * std::cos(phase);

        state.rho = density_;
        state.p = pressure_;
        state.u = 0.0;
        state.v = 0.0;
        state.w = 0.0;
        state.SetMassFraction(background_id_, 1.0 - tracer);
        state.SetMassFraction(tracer_id_, tracer);
    }
};

REGISTER_PROBLEM_CLASS("DiffusionMode", DiffusionModeProblem);
