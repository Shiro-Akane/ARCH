#include "numerics/diffusion/DiffFunction.h"
#include "numerics/diffusion/DiffFlux.h"
#include "numerics/diffusion/RKL1TimeIntegrator.h"
#include "numerics/diffusion/RKL2TimeIntegrator.h"
#include "numerics/burnsolver/ode_ros4.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "physics/eos/IdealGas.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
struct Ros4ScalarNetwork
{
    static constexpr int ODE_NEQ = 1;
    static constexpr int NUM_SPECIES = 0;
};

struct Ros4UnusedMatrix {};
struct Ros4UnusedLinearSolver {};
using Ros4Scalar = Solver_ROS4<
    Ros4ScalarNetwork, Ros4UnusedMatrix, Ros4UnusedLinearSolver>;

double ros4_amplification(double z)
{
    const double denominator = 1.0 - Ros4Scalar::gamma * z;
    const double u1 = Ros4Scalar::gamma * z / denominator;
    const double u2 = Ros4Scalar::gamma
        * (z * (1.0 + Ros4Scalar::a21 * u1) + Ros4Scalar::c21 * u1)
        / denominator;
    const double u3 = Ros4Scalar::gamma
        * (z * (1.0 + Ros4Scalar::a31 * u1 + Ros4Scalar::a32 * u2)
           + Ros4Scalar::c31 * u1 + Ros4Scalar::c32 * u2)
        / denominator;
    const double u4 = Ros4Scalar::gamma
        * (z * (1.0 + Ros4Scalar::a41 * u1 + Ros4Scalar::a42 * u2
                  + Ros4Scalar::a43 * u3)
           + Ros4Scalar::c41 * u1 + Ros4Scalar::c42 * u2
           + Ros4Scalar::c43 * u3)
        / denominator;
    return 1.0 + Ros4Scalar::m1 * u1 + Ros4Scalar::m2 * u2
        + Ros4Scalar::m3 * u3 + Ros4Scalar::m4 * u4;
}

struct PeriodicBoundary
{
    void apply(FluidState& state, const Grid& grid) const
    {
        const auto copy = [&](int source, int destination) {
            state.set(destination, state.get(source));
            for (int species = 0; species < state.GetNumSpecies(); ++species)
                state.X(species, destination) = state.X(species, source);
        };
        copy(grid.Ie() - 1, grid.Is() - 1);
        copy(grid.Is(), grid.Ie());
    }
};

struct NonlinearPressureEos
{
    double get_pressure(const FluidVector& U, const double*) const
    {
        const double kinetic = 0.5 * U.mom_u * U.mom_u / U.rho;
        return U.rho * (U.eng - kinetic);
    }

    double get_total_energy_primitive(
        double rho, double u, double, double, double pressure, const double*) const
    {
        return pressure / rho + 0.5 * rho * u * u;
    }
};

double rkl_amplification(DiffFunction::RKLOrder order, int stages, double z)
{
    const bool second_order = order == DiffFunction::RKLOrder::Second;
    const auto first = DiffFunction::get_rkl_coeffs(order, 1, stages);
    double older = 1.0;
    double previous = 1.0 + first.tilde_mu * z;

    for (int stage = 2; stage <= stages; ++stage) {
        const auto coeffs = DiffFunction::get_rkl_coeffs(order, stage, stages);
        const double current = coeffs.mu * previous + coeffs.nu * older
            + (second_order ? 1.0 - coeffs.mu - coeffs.nu : 0.0)
            + coeffs.tilde_mu * z * previous
            + (second_order ? coeffs.gamma * z : 0.0);
        older = previous;
        previous = current;
    }
    return previous;
}

FluidVector entropy_wave_average(int cell, int cells)
{
    const double dx = 1.0 / cells;
    const double sinc = std::sin(M_PI * dx) / (M_PI * dx);
    const double x = (cell + 0.5) * dx;
    const double rho = 1.0 + 0.2 * sinc * std::sin(2.0 * M_PI * x);
    return {rho, rho, 0.0, 0.0, 2.5 + 0.5 * rho};
}

double ppm_face_l1(int cells, double& pressure_error)
{
    double error = 0.0;
    pressure_error = 0.0;
    for (int cell = 0; cell < cells; ++cell) {
        const auto states = PPMReconstruction::apply(
            entropy_wave_average(cell - 2, cells),
            entropy_wave_average(cell - 1, cells),
            entropy_wave_average(cell, cells),
            entropy_wave_average(cell + 1, cells),
            entropy_wave_average(cell + 2, cells),
            entropy_wave_average(cell + 3, cells));
        const double exact = 1.0 + 0.2 * std::sin(2.0 * M_PI * (cell + 1.0) / cells);
        error += 0.5 * (std::abs(states.first.rho - exact)
                      + std::abs(states.second.rho - exact));
        for (const FluidVector* state : {&states.first, &states.second}) {
            const double kinetic = 0.5 * state->mom_u * state->mom_u / state->rho;
            const double pressure = 0.4 * (state->eng - kinetic);
            pressure_error = std::max(pressure_error, std::abs(pressure - 1.0));
        }
    }
    return error / cells;
}

void check_eos_aware_ppm()
{
    FluidState state;
    state.Preallocate(6);
    for (int cell = 0; cell < 6; ++cell) {
        const double rho = 0.8 + 0.1 * cell;
        state.set(cell, {rho, rho, 0.0, 0.0, 1.0 / rho + 0.5 * rho});
    }

    const NonlinearPressureEos eos;
    const auto face = PPMReconstruction::run_eos(
        state, eos, 2, 0, nullptr, nullptr, nullptr);
    if (std::abs(eos.get_pressure(face.first, nullptr) - 1.0) > 1e-12
        || std::abs(eos.get_pressure(face.second, nullptr) - 1.0) > 1e-12) {
        throw std::runtime_error("EOS-aware PPM failed to preserve constant pressure.");
    }
}

void check_single_block_diffusion_buffers()
{
    Grid grid(1, 0.0, 1.0);
    grid.dim = 1;
    grid.InitializeTopology();

    SpeciesManager species;
    const int background = species.add_species("background", 1.0, 1.0, 1.4, 717.5);
    const int tracer = species.add_species("tracer", 1.0, 1.0, 1.4, 717.5);
    IdealGas eos(1.4, species);

    FluidState initial;
    initial.Preallocate(grid.GetTotalSize());
    initial.InitSpecies(species.count());
    for (int i = grid.Is(); i < grid.Ie(); ++i) {
        const double x = grid.GetCellCenterX(i);
        const double tracer_fraction = 0.5 + 0.25 * std::cos(2.0 * M_PI * x);
        initial.set(i, {1.0, 0.0, 0.0, 0.0, 2.5});
        initial.X(background, i) = 1.0 - tracer_fraction;
        initial.X(tracer, i) = tracer_fraction;
    }

    SimConfig config;
    config.grid.dim = 1;
    config.grid.geometry = "cartesian";
    config.physics.diffusion.use_diffusion = true;
    config.physics.diffusion.use_species_diffusion = true;
    config.physics.diffusion.D_spec = 0.01;
    config.physics.diffusion.diff_cfl = 0.8;
    config.physics.diffusion.max_stages = 32;

    const PeriodicBoundary boundary;
    FluidState rkl1 = initial;
    FluidState rkl2 = initial;
    boundary.apply(rkl1, grid);
    boundary.apply(rkl2, grid);
    const double dt_forward_euler = DiffFlux::adaptive_dt_diff(
        initial, eos, grid, config, 1.0);
    RKL1TimeIntegrator::integrate(
        rkl1, eos, grid, config, 0.5 * dt_forward_euler,
        dt_forward_euler, boundary);
    RKL2TimeIntegrator::integrate(
        rkl2, eos, grid, config, 0.5 * dt_forward_euler,
        dt_forward_euler, boundary);

    for (const FluidState* result : {&rkl1, &rkl2}) {
        for (int i = grid.Is(); i < grid.Ie(); ++i) {
            const double sum = result->X(background, i) + result->X(tracer, i);
            if (!std::isfinite(result->X(tracer, i)) || std::abs(sum - 1.0) > 1e-12)
                throw std::runtime_error("Single-block RKL diffusion produced an invalid state.");
        }
    }
}
}

int main()
{
    const double ros4_z = 1.0e-2;
    if (std::abs(ros4_amplification(ros4_z) - std::exp(ros4_z)) > 1.0e-10)
        throw std::runtime_error("ROS4 tableau/stage scaling is not fourth-order consistent.");
    if (std::abs(ros4_amplification(-1.0e6)) > 1.0e-4)
        throw std::runtime_error("ROS4 tableau lost its L-stable limit.");

    for (int stages = 1; stages <= 16; ++stages) {
        const double factor = stages * (stages + 1.0) / 2.0;
        const double reported = DiffFunction::stable_step_rkl1(1.0, 1.0, stages);
        if (std::abs(reported - factor) > 1e-14 * factor)
            throw std::runtime_error("RKL1 stable-step factor is inconsistent.");

        const double endpoint = rkl_amplification(
            DiffFunction::RKLOrder::First, stages, -2.0 * factor);
        if (std::abs(endpoint) > 1.0 + 1e-12)
            throw std::runtime_error("RKL1 polynomial exceeds its stability interval.");

        const double z = 1e-6;
        const double first_derivative =
            (rkl_amplification(DiffFunction::RKLOrder::First, stages, z) - 1.0) / z;
        if (std::abs(first_derivative - 1.0) > 2e-5)
            throw std::runtime_error("RKL1 polynomial is not first-order consistent.");
    }

    for (int stages : {2, 3, 5, 9}) {
        const double z = 1e-4;
        const double amplification = rkl_amplification(
            DiffFunction::RKLOrder::Second, stages, z);
        const double quadratic = (amplification - 1.0 - z) / (z * z);
        if (std::abs(quadratic - 0.5) > 2e-4)
            throw std::runtime_error("RKL2 polynomial is not second-order consistent.");
    }

    double pressure_error_64 = 0.0;
    double pressure_error_128 = 0.0;
    const double ppm_error_64 = ppm_face_l1(64, pressure_error_64);
    const double ppm_error_128 = ppm_face_l1(128, pressure_error_128);
    const double ppm_rate = std::log2(ppm_error_64 / ppm_error_128);
    if (ppm_rate < 3.8)
        throw std::runtime_error("PPM smooth face reconstruction lost high-order accuracy.");
    if (std::max(pressure_error_64, pressure_error_128) > 1e-12)
        throw std::runtime_error("PPM failed to preserve a constant-pressure contact.");

    check_eos_aware_ppm();
    check_single_block_diffusion_buffers();

    std::cout << "RKL polynomials/single-block updates and EOS-aware PPM passed.\n";
    return 0;
}
