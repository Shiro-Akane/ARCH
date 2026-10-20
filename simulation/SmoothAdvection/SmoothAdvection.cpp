/**
 * @file SmoothAdvection.cpp
 * @brief Initialize a cell-averaged entropy wave for hydro reconstruction checks.
 *
 * Setup selects an explicit linear axis for multidimensional modes and computes
 * the uniform-root-cell averaging factor for the density mode. Init
 * keeps pressure and velocity uniform; the configured boundary conditions and
 * common hydro policies determine the subsequent advection.
 */

#include <cmath>
#include <iostream>
#include <stdexcept>

#include <UserInterface.h>

#include <GlobalDefs.h>

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
    int mode_axis_ = 0; // Setup selects a declared linear axis; zero is unprepared.
    bool cylindrical_axis_ = false;

public:
    static arch::config::CaseConfiguration DescribeConfiguration(
        const arch::config::StandardInputResolution& inputs)
    {
        arch::config::CaseConfiguration result;
        result.complete = true;
        result.consumers.needs_network = false;
        result.consumers.needs_temperature_floor = false;
        result.consumers.needs_composition_floor = false;
        result.parameters = {
            {"rho_mean", "float", "g/cm^3", "verification"},
            {"rho_amplitude", "float", "g/cm^3", "verification"},
            {"pressure0", "float", "erg/cm^3", "verification"},
            {"velocity0", "float", "cm/s", "verification"},
            {"mode", "int", "1", "verification"}};
        // The old Cartesian 1-D model defines axis1. Multidimensional
        // verification requires an explicit case-owned axis, never a default.
        const auto* second = arch::config::input_detail::get<int>(inputs, "nblockx2");
        arch::config::CaseParameter axis{"mode_axis", "int", "1", "verification"};
        axis.requirement = second
            ? arch::config::ConditionResult{*second > 0, {}}
            : arch::config::ConditionResult{std::nullopt, {"nblockx2"}};
        result.parameters.push_back(std::move(axis));
        return result;
    }

    void Setup(SimConfig& config, SpeciesManager&)
    {
        cylindrical_axis_ = config.grid.geometry == "cylindrical";
        const bool legacy_line = config.grid.dim == 1 && config.grid.geometry == "cartesian";
        if (legacy_line) {
            mode_axis_ = 1; // Existing model definition, with the original arithmetic below.
            if (config.DeclaredInputTokens().contains("mode_axis") &&
                config.Get<int>("mode_axis", 0) != 1)
                throw std::invalid_argument("SmoothAdvection: inactive mode_axis in Cartesian 1-D");
        } else {
            // Get's second argument is audit metadata; loaded O7 inputs never
            // use it as a fallback. An unresolved/undeclared axis throws.
            mode_axis_ = config.Get<int>("mode_axis", 0);
            if (mode_axis_ < 1 || mode_axis_ > config.grid.dim ||
                (config.grid.geometry != "cartesian" &&
                 !(cylindrical_axis_ && config.grid.dim >= 2 && mode_axis_ == 2)))
                throw std::invalid_argument("SmoothAdvection requires an active Cartesian axis or cylindrical axial axis2");
            // The root-cell sinc is exact for uniform root cells. It is not
            // a child-cell average: dynamic/refined tests use other fixtures.
            if (config.amr.lrefinemin != 0 || config.amr.lrefinemax != 0)
                throw std::invalid_argument("SmoothAdvection multidimensional analytic mode requires a uniform root grid");
            const std::string lower[]{config.grid.x1l_boundary_type, config.grid.x2l_boundary_type, config.grid.x3l_boundary_type};
            const std::string upper[]{config.grid.x1r_boundary_type, config.grid.x2r_boundary_type, config.grid.x3r_boundary_type};
            if (lower[mode_axis_-1] != "periodic" || upper[mode_axis_-1] != "periodic")
                throw std::invalid_argument("SmoothAdvection analytic mode requires periodic selected-axis faces");
        }
        const double lower[]{config.grid.x1_min, config.grid.x2_min, config.grid.x3_min};
        const double upper[]{config.grid.x1_max, config.grid.x2_max, config.grid.x3_max};
        x_min_ = lower[mode_axis_-1];
        length_ = upper[mode_axis_-1] - x_min_;
        rho_mean_ = config.Get<double>("rho_mean", 1.0);
        rho_amplitude_ = config.Get<double>("rho_amplitude", 0.2);
        pressure_ = config.Get<double>("pressure0", 1.0);
        velocity_ = config.Get<double>("velocity0", 1.0);
        mode_ = config.Get<int>("mode", 1);

        if (length_ <= 0.0 || mode_ < 1 || rho_mean_ <= std::abs(rho_amplitude_)) {
            throw std::invalid_argument("SmoothAdvection parameters require L>0, mode>=1, and positive density.");
        }

        const double cell_width = ProblemHelper::GetRootCellWidth(config, mode_axis_);
        const double half_cell_phase = arch::constants::math::pi * mode_ * cell_width / length_;
        cell_average_factor_ = std::sin(half_cell_phase) / half_cell_phase;

        std::cout << "[Problem] Smooth entropy wave: rho=" << rho_mean_
                  << "+" << rho_amplitude_ << " sin(2*pi*" << mode_ << "*x/L)"
                  << ", u=" << velocity_ << ", p=" << pressure_ << std::endl;
    }

    void Init(const PointCoords& point, PrimitiveData& state) const
    {
        // Cylindrical logical axis2 is native z, carried by physical z;
        // Cartesian axes retain x/y/z. No radius/angle projection is applied.
        const double coordinate = mode_axis_ == 1 ? point.x :
            (mode_axis_ == 2 ? (cylindrical_axis_ ? point.z : point.y) : point.z);
        const double phase = 2.0 * arch::constants::math::pi * mode_ * (coordinate - x_min_) / length_;
        state.rho = rho_mean_ + rho_amplitude_ * cell_average_factor_ * std::sin(phase);
        state.p = pressure_;
        state.u = mode_axis_ == 1 ? velocity_ : 0.0;
        state.v = mode_axis_ == 2 ? velocity_ : 0.0;
        state.w = mode_axis_ == 3 ? velocity_ : 0.0;
    }
};

REGISTER_PROBLEM_CLASS("SmoothAdvection", SmoothAdvectionProblem);
