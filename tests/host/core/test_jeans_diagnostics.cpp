/**
 * @file test_jeans_diagnostics.cpp
 * @brief CPU numeric qualification of the isolated Jeans formula.
 * Independent Decimal 80/120-digit references use published pi and CGS G.
 * The 16-double-epsilon check is an engineering rounding check, not an EOS,
 * AMR threshold, trajectory or scientific acceptance budget.
 */
#include "physics/diagnostics/JeansDiagnostics.h"
#include "physics/eos/IdealGas.h"
#include <array>
#include <limits>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>

int main()
{
    struct Case { double rho, cs2, h, expected; };
    const std::array<Case, 8> cases{{
        {0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.accc1f12f6cbdp+12},
        {0x1.0000000000000p+2, 0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.accc1f12f6cbdp+11},
        {0x1.0000000000000p+0, 0x1.0000000000000p+2, 0x1.0000000000000p+0, 0x1.accc1f12f6cbdp+13},
        {0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.0000000000000p+2, 0x1.accc1f12f6cbdp+10},
        {0x0.00000000007e8p-1022, 0x1.56e1fc2f8f359p-997, 0x1.5af1d78b58c40p+66, 0x1.70560f248ababp-21},
        {0x1.7e43c8800759cp+996, 0x1.7e43c8800759cp+996, 0x1.0000000000000p+0, 0x1.accc1f12f6cbdp+12},
        {0x1.56e1fc2f8f359p-997, 0x1.7e43c8800759cp+996, 0x1.7e43c8800759cp+996, 0x1.accc1f12f6cbcp+12},
        {0x1.7e43c8800759cp+996, 0x1.56e1fc2f8f359p-997, 0x1.56e1fc2f8f359p-997, 0x1.accc1f12f6cbcp+12}
    }};
    for (const auto& c : cases) {
        const auto result = JeansDiagnostics::evaluate(c.rho, c.cs2, c.h);
        if (result.status != JeansDiagnostics::Status::valid ||
            std::abs(result.cells - c.expected) >
                16 * std::numeric_limits<double>::epsilon() * c.expected)
            throw std::runtime_error("Jeans numeric reference mismatch");
    }
    for (double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        for (const auto& args : {std::array<double,3>{invalid,1,1},
                                std::array<double,3>{1,invalid,1},
                                std::array<double,3>{1,1,invalid}})
            if (JeansDiagnostics::evaluate(args[0],args[1],args[2]).status !=
                JeansDiagnostics::Status::invalid_input)
                throw std::runtime_error("Invalid Jeans numeric input accepted");
    }
    const auto overflow = JeansDiagnostics::evaluate(1e-300,1e300,1e-300);
    const auto underflow = JeansDiagnostics::evaluate(1e300,1e-300,1e300);
    if (overflow.status != JeansDiagnostics::Status::unrepresentable ||
        underflow.status != JeansDiagnostics::Status::unrepresentable)
        throw std::runtime_error("Jeans nonrepresentable result hidden");
    // Independent spacing expectations for the maintained native coordinates.
    // Do not use production PhysicalSpacing to construct the expected result.
    GridMetrics::GeometryView grid{};
    grid.dx1 = 1.0; grid.dx2 = 3.0; grid.dx3 = 4.0; grid.x1_min = 1.5;
    const auto check_spacing = [&](double expected_spacing) {
        const auto actual = JeansDiagnostics::evaluate_cell(1, 1, grid, 0, 0);
        const double expected = cases[0].expected / expected_spacing;
        if (actual.status != JeansDiagnostics::Status::valid ||
            std::abs(actual.cells - expected) >
                16 * std::numeric_limits<double>::epsilon() * expected)
            throw std::runtime_error("Jeans physical spacing mismatch");
    };
    grid.dim = 1; check_spacing(1.0);
    // Inactive axes may contain unusable values and must never be evaluated.
    grid.dx2 = std::numeric_limits<double>::quiet_NaN();
    grid.dx3 = -1.0; check_spacing(1.0);
    grid.dx2 = 3.0; grid.dim = 2; check_spacing(3.0);
    grid.dx3 = 4.0; grid.dim = 3; check_spacing(4.0);
    grid.geometry = GridMetrics::Geometry::Cylindrical;
    grid.dim = 2; check_spacing(6.0); // r=2, polar arc r*dphi.
    grid.dim = 3; check_spacing(8.0); // (r,z,phi), largest r*dphi.
    grid.geometry = GridMetrics::Geometry::Spherical;
    grid.dim = 2; check_spacing(6.0); // Existing 2-D polar specialization.
    grid.dim = 3; grid.dx2 = .25;
    grid.x2_min = arch::constants::math::pi / 2 - .125;
    check_spacing(8.0);
    grid.x2_min = 0.0; grid.dx2 = .01;
    check_spacing(1.0); // Near polar axis: radial spacing wins, no floor.
    grid.x1_min = 0.0; grid.dx1 = .25; grid.dx2 = .1; grid.dx3 = .1;
    check_spacing(.25); // First radial cell: no origin/radius repair.
    for (int dimension : {0, 4}) {
        grid.dim = dimension;
        if (JeansDiagnostics::evaluate_cell(1,1,grid,0,0).status !=
                JeansDiagnostics::Status::invalid_input)
            throw std::runtime_error("Invalid Jeans dimension accepted");
    }
    grid.dim = 2; grid.geometry = GridMetrics::Geometry::Cartesian;
    for (double spacing : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        grid.dx2 = spacing;
        if (JeansDiagnostics::evaluate_cell(1,1,grid,0,0).status !=
                JeansDiagnostics::Status::invalid_input)
            throw std::runtime_error("Invalid active spacing hidden by max");
    }
    grid.geometry = GridMetrics::Geometry::Unsupported; grid.dim = 1;
    if (JeansDiagnostics::evaluate_cell(1,1,grid,0,0).status !=
            JeansDiagnostics::Status::invalid_input)
        throw std::runtime_error("Unknown Jeans geometry accepted");
    // Independent caloric IdealGas reference, including moving fluid and a
    // two-species mixture whose gamma differs from the constructor fallback.
    // No production EOS function contributes to the expected expression.
    SpeciesManager empty_species;
    IdealGas simple(1.5, empty_species);
    SpeciesManager mixture_species;
    mixture_species.add_species("first", 1.0, 1.0, 1.5, 2.0);
    mixture_species.add_species("second", 2.0, 1.0, 2.0, 4.0);
    IdealGas mixture(1.4, mixture_species);
    const std::array<double,2> composition{.25,.75};
    const long double reference_pi =
        3.141592653589793238462643383279502884L;
    const long double reference_G = 6.67430e-8L;
    double max_relative_error = 0.0;
    int eos_cases = 0;
    for (bool use_mixture : {false,true})
    for (double density : {.25,1.0,4.0})
    for (double specific_internal_energy : {4.0,16.0})
    for (bool moving : {false,true}) {
        const auto& eos = use_mixture ? mixture : simple;
        const double* Xi = use_mixture ? composition.data() : nullptr;
        const double u = moving ? 2.0 : 0.0;
        const double v = moving ? 3.0 : 0.0;
        const double w = moving ? 4.0 : 0.0;
        const FluidVector state{density,density*u,density*v,density*w,
            density*(specific_internal_energy + .5*(u*u+v*v+w*w))};
        const double pressure = eos.get_pressure(state,Xi);
        const double sound_speed = eos.get_sound_speed(state,pressure,Xi);
        GridMetrics::GeometryView physical_grid{};
        physical_grid.dim = 2; physical_grid.dx1 = .25; physical_grid.dx2 = .5;
        const auto actual = JeansDiagnostics::evaluate_cell(
            state.rho,sound_speed*sound_speed,physical_grid,0,0);
        // Cv-weighted gamma = 1 + (1/4*2*1/2 + 3/4*4*1)/(1/4*2 + 3/4*4).
        const long double gamma = use_mixture ? 27.0L/14.0L : 1.5L;
        const long double expected = std::sqrt(reference_pi *
            gamma*(gamma-1)*specific_internal_energy/(reference_G*density))/.5L;
        const double relative_error = static_cast<double>(
            std::abs(static_cast<long double>(actual.cells)-expected)/expected);
        max_relative_error = std::max(max_relative_error,relative_error);
        if (actual.status != JeansDiagnostics::Status::valid ||
            relative_error > 16*std::numeric_limits<double>::epsilon())
            throw std::runtime_error("Jeans IdealGas independent reference mismatch");
        ++eos_cases;
    }
    std::cout << std::setprecision(17) << "JEANS_IDEALGAS_CASES=" << eos_cases
              << " MAX_RELATIVE_ERROR=" << max_relative_error << '\n';
    std::cout << "JEANS_DIAGNOSTICS_NUMERIC_PASS\n";
}
