/**
 * @file test_refinement_indicator_math.cpp
 * @brief Check refinement indicators and composition roundoff handling.
 *
 * Resolve physical gradients across density and species scales while ensuring
 * that closure noise alone does not request refinement.
 */
#include "amr/refinement/RefinementIndicatorMath.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

/** Physical center velocities override only the transient diagnostic reader.
 * Independent rigid rotation curl=2*Omega, u_r=alpha*r div=2*alpha and
 * u_z=z div=1 hold on positive cells and actual signed-axis ghosts. The
 * moment arrays deliberately differ, proving they cannot be used as a point
 * velocity oracle. These are given-field math tests, not Runtime publication.
 */


int main()
{
    using namespace amr::indicator;
    // Public native selector must preserve VELZ and reject it on the retained
    // two-component charts; no default geometry is supplied to this API.
    AmrConfig selected;selected.refine_on_rho=false;selected.refine_on_velz=true;
    const auto native=make_selection(selected,2,{},GridMetrics::Geometry::Cylindrical);
    require(native.size()==1 && native.front().field==Field::VelocityZ,"native Cyl2 VELZ selector lost phi");
    for(const auto geometry:{GridMetrics::Geometry::Cartesian,GridMetrics::Geometry::Spherical}) {
        bool rejected=false;
        try{(void)make_selection(selected,2,{},geometry);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"retained two-component chart accepted VELZ");
    }
    require(loehner_error(0, 0, 0) == 0, "zero field");
    require(std::abs(loehner_error(1, 2, 4) - 1.0 / 3.09) < 1.e-15,
            "ordinary scalar formula changed");
    require(std::isnan(loehner_error(0, 0, 0, -1)), "invalid uncertainty accepted");
    require(std::isnan(loehner_error(0, INFINITY, 0)), "nonfinite field accepted");
    for (int count : {1, 7, 19, 31, 200}) {
        for (double rho : {1.e-20, 1.0, 1.e20}) {
            const double noise = rho * composition_roundoff(count);
            for (double sign : {-1.0, 1.0}) {
                require(loehner_error(0, sign * noise, 0, 4 * noise) == 0,
                        "closure roundoff drives refinement");
                const double trace = loehner_error(0, sign * rho * 1.e-10, 0, 4 * noise);
                require(trace > .98, "resolvable trace gradient suppressed");
            }
        }
    }
    // Exercise the actual field selector, equally for every species, not only
    // a special final-species function. These are mass fractions, not rhoX:
    // changing the density unit must never change a species-indicator flag.
    constexpr int count = 19, cells = 3;
    std::array<double, cells> rho{};
    std::array<double, count * cells> composition{};
    StateView state{};
    state.density = rho.data(); state.species = composition.data();
    state.cells = cells; state.species_count = count;
    GridMetrics::GeometryView grid{};
    grid.dim = 1; grid.dx1 = 1; grid.total_size = cells;
    for (double scale : {1.e-20, 1.0, 1.e20}) {
        rho.fill(scale);
        for (int species = 0; species < count; ++species) {
            composition.fill(0);
            const Selection selected{Field::Species, species};
            composition[species * cells + 1] = 1.e-16;
            require(cell_error(state, grid, &selected, 1, 1, 0, 0) == 0,
                    "production selector retained zero-field noise");
            composition[species * cells + 1] = 1.e-10;
            require(cell_error(state, grid, &selected, 1, 1, 0, 0) > .98,
                    "production selector lost trace field");
        }
    }
    std::cout << "REFINEMENT_INDICATOR_MATH_PASS\n";
}
