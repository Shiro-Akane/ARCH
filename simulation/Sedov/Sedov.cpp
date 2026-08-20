/**
 * @file Sedov.cpp
 * @brief Regularized Cartesian Sedov blast benchmark.
 *
 * A finite-radius pressure deposit initializes the blast. The requested energy
 * is normalized by the continuous 1D/2D/3D injection measure;
 * cell-centre sampling introduces a resolution-dependent deposition error for
 * the planned validation analysis.
 */

#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"

class SedovProblem
{
    int dim_ = 1;
    double center_x_ = 0.0;
    double center_y_ = 0.0;
    double center_z_ = 0.0;
    double deposit_radius_ = 0.0;
    double ambient_density_ = 1.0;
    double ambient_pressure_ = 1.0e-5;
    double deposit_pressure_ = 0.0;
    int gas_id_ = -1;

public:
    void Setup(SimConfig &config, SpeciesManager &specs)
    {
        if (config.grid.geometry != "cartesian") {
            throw std::invalid_argument(
                "Sedov benchmark currently supports geometry = cartesian only.");
        }

        dim_ = config.grid.dim;
        center_x_ = config.Get<double>(
            "center_x", 0.5 * (config.grid.x1_min + config.grid.x1_max));
        center_y_ = config.Get<double>(
            "center_y", 0.5 * (config.grid.x2_min + config.grid.x2_max));
        center_z_ = config.Get<double>(
            "center_z", 0.5 * (config.grid.x3_min + config.grid.x3_max));
        deposit_radius_ = config.Get<double>("deposit_radius", 0.03);
        ambient_density_ = config.Get<double>("ambient_density", 1.0);
        ambient_pressure_ = config.Get<double>("ambient_pressure", 1.0e-5);
        const double explosion_energy = config.Get<double>("explosion_energy", 1.0);

        if (deposit_radius_ <= 0.0 || ambient_density_ <= 0.0 ||
            ambient_pressure_ <= 0.0 || explosion_energy <= 0.0) {
            throw std::invalid_argument(
                "Sedov requires positive radius, density, pressure, and energy.");
        }
        if (config.physics.gamma <= 1.0) {
            throw std::invalid_argument("Sedov ideal-gas gamma must be greater than one.");
        }

        double injection_measure = 2.0 * deposit_radius_;
        const double pi = std::numbers::pi_v<double>;
        if (dim_ == 2) {
            // Energy per unit depth for a Cartesian circular blast.
            injection_measure = pi * deposit_radius_ * deposit_radius_;
        } else if (dim_ == 3) {
            injection_measure = (4.0 / 3.0) * pi * deposit_radius_ *
                                deposit_radius_ * deposit_radius_;
        }

        deposit_pressure_ = ambient_pressure_ +
            (config.physics.gamma - 1.0) * explosion_energy / injection_measure;
        gas_id_ = specs.add_species(
            "SedovGas", 1.0, 1.0, config.physics.gamma, 1.0);

        std::cout << "[Problem] Sedov regularized blast: dim=" << dim_
                  << ", radius=" << deposit_radius_
                  << ", deposited pressure=" << deposit_pressure_ << "\n";
    }

    void Init(const PointCoords &point, PrimitiveData &out) const
    {
        double distance2 = (point.x - center_x_) * (point.x - center_x_);
        if (dim_ >= 2) {
            distance2 += (point.y - center_y_) * (point.y - center_y_);
        }
        if (dim_ == 3) {
            distance2 += (point.z - center_z_) * (point.z - center_z_);
        }

        out.rho = ambient_density_;
        out.p = distance2 <= deposit_radius_ * deposit_radius_
            ? deposit_pressure_ : ambient_pressure_;
        out.u = 0.0;
        out.v = 0.0;
        out.w = 0.0;
        out.SetMassFraction(gas_id_, 1.0);
    }
};

REGISTER_PROBLEM_CLASS("Sedov", SedovProblem);
