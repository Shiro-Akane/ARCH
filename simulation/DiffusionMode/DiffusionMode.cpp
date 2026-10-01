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
#include <iostream>
#include <stdexcept>

#include <UserInterface.h>

#include <GlobalDefs.h>

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
    static arch::config::CaseConfiguration DescribeConfiguration(
        const arch::config::StandardInputResolution&)
    {
        arch::config::CaseConfiguration result;
        result.complete = true;
        result.consumers.needs_network = false;
        result.consumers.needs_temperature_floor = false;
        result.consumers.needs_composition_floor = false;
        result.parameters = {
            {"rho0", "float", "g/cm^3"},
            {"pressure0", "float", "erg/cm^3"},
            {"tracer_mean", "float", "1"},
            {"tracer_amplitude", "float", "1"},
            {"mode", "int", "1"}};
        return result;
    }

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

        background_id_ = species.add_species("background", config.MaterialConstant(1.0, "background.A"), config.MaterialConstant(1.0, "background.Z"), config.MaterialInput("gamma"), config.MaterialConstant(717.5, "background.Cv"));
        tracer_id_ = species.add_species("tracer", config.MaterialConstant(1.0, "tracer.A"), config.MaterialConstant(1.0, "tracer.Z"), config.MaterialInput("gamma"), config.MaterialConstant(717.5, "tracer.Cv"));

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
