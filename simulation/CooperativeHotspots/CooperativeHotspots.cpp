/**
 * @file CooperativeHotspots.cpp
 * @brief Initialize a controlled two-dimensional isobaric hotspot experiment.
 *
 * Setup brackets hotspot density against the ambient pressure using the
 * selected common EOS and network composition. Init chooses the configured
 * hotspot footprint and supplies stationary primitive states; the production
 * driver owns subsequent hydro and burn evolution.
 */

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"

class CooperativeHotspots
{
    double rho_ambient_ = 1.0e7;
    double temperature_ambient_ = 2.0e8;
    double pressure_ambient_ = 0.0;
    double rho_hot_ = 0.0;
    double temperature_hot_ = 3.0e9;
    double pressure_hot_ = 0.0;

    double center_x_ = 0.0;
    double center_y_ = 0.0;
    double radius_ = 8.0;
    double separation_ = 24.0;
    std::string mode_ = "single";
    std::vector<double> composition_;

    double solve_isobaric_density(const SimConfig &config,
                                  const SpeciesManager &species) const
    {
        // Pressure is monotone in density over the intended Helmholtz states.
        // Solve in log(rho) so the bracket covers both radiation- and
        // degeneracy-dominated hotspot states without a scale-dependent step.
        double rho_low = rho_ambient_ * 1.0e-8;
        double rho_high = rho_ambient_;
        double p_low = ProblemHelper::GetPressureFromRhoT(
            config, species, rho_low, temperature_hot_, composition_.data());
        double p_high = ProblemHelper::GetPressureFromRhoT(
            config, species, rho_high, temperature_hot_, composition_.data());

        if (p_low > pressure_ambient_ || p_high < pressure_ambient_) {
            throw std::runtime_error(
                "No isobaric hotspot density is bracketed. Reduce hotspot_temperature "
                "or provide a compatible ambient state.");
        }

        double log_low = std::log(rho_low);
        double log_high = std::log(rho_high);
        for (int iteration = 0; iteration < 80; ++iteration) {
            const double log_mid = 0.5 * (log_low + log_high);
            const double rho_mid = std::exp(log_mid);
            const double p_mid = ProblemHelper::GetPressureFromRhoT(
                config, species, rho_mid, temperature_hot_, composition_.data());
            if (p_mid < pressure_ambient_)
                log_low = log_mid;
            else
                log_high = log_mid;
        }
        return std::exp(0.5 * (log_low + log_high));
    }

    bool inside_hot_region(double x, double y) const
    {
        const double dx = x - center_x_;
        const double dy = y - center_y_;
        const double radius2 = radius_ * radius_;

        if (mode_ == "single")
            return dx * dx + dy * dy <= radius2;

        if (mode_ == "double") {
            const double dx_left = dx + 0.5 * separation_;
            const double dx_right = dx - 0.5 * separation_;
            return dx_left * dx_left + dy * dy <= radius2 ||
                   dx_right * dx_right + dy * dy <= radius2;
        }

        if (mode_ == "equal_energy") {
            const double equal_radius = std::sqrt(2.0) * radius_;
            return dx * dx + dy * dy <= equal_radius * equal_radius;
        }

        if (mode_ == "elongated") {
            // Area pi*(2R)*R equals the total area of two non-overlapping
            // radius-R circles, while retaining a single connected kernel.
            const double nx = dx / (2.0 * radius_);
            const double ny = dy / radius_;
            return nx * nx + ny * ny <= 1.0;
        }

        throw std::runtime_error("Unknown hotspot_mode: " + mode_);
    }

public:
    void Setup(SimConfig &config, SpeciesManager &species)
    {
        if (config.grid.dim != 2 || config.grid.geometry != "cartesian")
            throw std::invalid_argument(
                "CooperativeHotspots currently requires a 2D Cartesian grid.");

        rho_ambient_ = config.Get<double>("ambient_density", 1.0e7);
        temperature_ambient_ = config.Get<double>("ambient_temperature", 2.0e8);
        temperature_hot_ = config.Get<double>("hotspot_temperature", 3.0e9);
        center_x_ = config.Get<double>(
            "hotspot_center_x", 0.5 * (config.grid.x1_min + config.grid.x1_max));
        center_y_ = config.Get<double>(
            "hotspot_center_y", 0.5 * (config.grid.x2_min + config.grid.x2_max));
        radius_ = config.Get<double>("hotspot_radius", 8.0);
        separation_ = config.Get<double>("hotspot_separation", 24.0);
        mode_ = config.Get<std::string>("hotspot_mode", "single");
        std::transform(mode_.begin(), mode_.end(), mode_.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (!(rho_ambient_ > 0.0) || !(temperature_ambient_ > 0.0) ||
            !(temperature_hot_ > temperature_ambient_) || !(radius_ > 0.0) ||
            !(separation_ >= 0.0)) {
            throw std::invalid_argument("Invalid cooperative-hotspot state or geometry.");
        }
        if (mode_ != "single" && mode_ != "double" &&
            mode_ != "equal_energy" && mode_ != "elongated") {
            throw std::invalid_argument(
                "hotspot_mode must be single, double, equal_energy, or elongated.");
        }
        if (mode_ == "double" && separation_ < 2.0 * radius_) {
            throw std::invalid_argument(
                "Double hotspots must not overlap in the controlled pilot.");
        }

        ProblemHelper::SetupNetworkAndFractions(config, species, composition_);
        pressure_ambient_ = ProblemHelper::GetPressureFromRhoT(
            config, species, rho_ambient_, temperature_ambient_, composition_.data());
        rho_hot_ = solve_isobaric_density(config, species);
        pressure_hot_ = ProblemHelper::GetPressureFromRhoT(
            config, species, rho_hot_, temperature_hot_, composition_.data());

        const double pressure_mismatch =
            std::abs(pressure_hot_ - pressure_ambient_) / pressure_ambient_;
        if (!std::isfinite(pressure_mismatch) || pressure_mismatch > 1.0e-10)
            throw std::runtime_error("Isobaric hotspot solve did not converge.");

        std::cout << "[Problem] Cooperative helium-hotspot pilot\n"
                  << "          Mode              : " << mode_ << "\n"
                  << "          Radius/separation : " << radius_ << " / " << separation_ << " cm\n"
                  << "          Ambient rho/T/P   : " << rho_ambient_ << " / "
                  << temperature_ambient_ << " / " << pressure_ambient_ << "\n"
                  << "          Hotspot rho/T/P   : " << rho_hot_ << " / "
                  << temperature_hot_ << " / " << pressure_hot_ << "\n"
                  << "          Relative dP/P     : " << pressure_mismatch << std::endl;
    }

    void Init(const PointCoords &point, PrimitiveData &state) const
    {
        const bool hot = inside_hot_region(point.x, point.y);
        state.rho = hot ? rho_hot_ : rho_ambient_;
        state.p = pressure_ambient_;
        state.u = 0.0;
        state.v = 0.0;
        state.w = 0.0;
        state.SetTemperature(hot ? temperature_hot_ : temperature_ambient_);
        state.mass_fractions = composition_;
    }
};

REGISTER_PROBLEM_CLASS("CooperativeHotspots", CooperativeHotspots);
