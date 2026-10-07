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
void native_physical_velocity_contract()
{
    using namespace amr::indicator;
    constexpr int nx=8,ny=8,cells=nx*ny;
    std::array<double,cells> rho{},mr{},mz{},mphi{};
    std::array<std::array<double,cells>,3> velocity{};
    auto geometry=GridMetrics::make_geometry_view(GridMetrics::Geometry::Cylindrical,
        2,{0.,0.,0.},{.25,.25,1.});
    geometry=GridMetrics::make_rz_geometry_view(geometry);
    geometry.ng=2;geometry.stride_y=nx;geometry.stride_z=cells;geometry.total_size=cells;
    StateView state{};state.density=rho.data();state.cells=cells;
    state.momentum[0]=mr.data();state.momentum[1]=mz.data();state.momentum[2]=mphi.data();
    for(int axis=0;axis<3;++axis)state.physical_velocity[axis]=velocity[axis].data();
    constexpr double omega=2.,alpha=.25;
    const auto fill=[&](bool radial,bool axial) {
        for(int j=0;j<ny;++j)for(int i=0;i<nx;++i) {
            const int cell=geometry.GetIndex(i,j,0);
            const double r=geometry.GetCellCenterX(i),z=geometry.GetCellCenterY(j);
            rho[cell]=1.;mr[cell]=7.;mz[cell]=9.;mphi[cell]=11.;
            velocity[0][cell]=radial?alpha*r:0.;
            velocity[1][cell]=axial?z:0.;velocity[2][cell]=omega*r;
        }
    };
    fill(false,false);
    for(int i:{1,2,3,5}) {
        require(std::abs(state.value({Field::Vorticity},geometry,i,3,0)-2.*omega)<1.e-15,
                "Native signed-ghost rigid rotation curl changed");
        require(state.value({Field::Divergence},geometry,i,3,0)==0.,
                "Native rotation generated divergence");
        require(state.value({Field::VelocityZ},geometry,i,3,0)==velocity[2][geometry.GetIndex(i,3,0)],
                "physical diagnostic reader fell back to stored J/W");
    }
    fill(true,false);
    for(int i:{1,2,3,5})require(std::abs(state.value({Field::Divergence},geometry,i,3,0)-2.*alpha)<1.e-15,
        "Native radial affine flow divergence is not 2*alpha");
    fill(false,true);
    for(int i:{1,2,3,5})require(std::abs(state.value({Field::Divergence},geometry,i,3,0)-1.)<1.e-15,
        "Native axial affine flow used polar face metrics");
    // A genuine annulus uses the same point field/geometry formulas.
    geometry.x1_min=1.;fill(false,false);
    require(std::abs(state.value({Field::Vorticity},geometry,3,3,0)-2.*omega)<1.e-15,
        "Native annulus rigid rotation curl changed");
    state.physical_velocity[2]=nullptr;
    require(state.value({Field::VelocityZ},geometry,3,3,0)==11.,
        "default velocity reader changed original momentum/rho arithmetic");
    geometry.x1_min=-.125; // i=ng has exactly zero represented center.
    state.physical_velocity[2]=velocity[2].data();fill(false,false);
    require(std::isnan(state.value({Field::Vorticity},geometry,geometry.ng,3,0)),
        "Native zero-radius diagnostic accepted a singular point");
}

int main()
{
    using namespace amr::indicator;
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
    native_physical_velocity_contract();
    std::cout << "REFINEMENT_INDICATOR_MATH_PASS\n";
}
