/**
 * @file test_curvilinear_metrics.cpp
 * @brief Check shared geometry and source terms against analytic measures.
 *
 * The cases span Cartesian, cylindrical and spherical coordinates, including
 * thin shells, poles and radial-origin behavior.
 */
#include "driver/DriverUtils.h"
#include "numerics/integrator/GeometricSources.h"
#include "math/geometry/CurvilinearMetricCases.h"
#include "math/geometry/RzMetricCases.h"
#include "math/geometry/ViscousGeometryCases.h"
#include "numerics/diffusion/DiffFlux.h"
#include "physics/eos/IdealGas.h"
#include <iostream>
#include <stdexcept>

namespace {
void close(double actual, double expected, const char* name) {
    if (!std::isfinite(actual) || std::abs(actual - expected) >
        2.e-12 * std::max(1.0, std::abs(expected)))
        throw std::runtime_error(name);
}
struct ConstantEos {
    double get_pressure(const FluidVector&, const double*) const { return 5.0; }
    double get_sound_speed(const FluidVector&, double, const double*) const { return 2.0; }
};
}

int main()
{
    using namespace GridMetrics;
    const double conditioning = CurvilinearMetricCases::conditioning_error();
    if (!std::isfinite(conditioning) || conditioning > 2.e-12)
        throw std::runtime_error("independent thin-shell/polar measures");
    std::cout << "INDEPENDENT_METRIC_MAX_RELATIVE_ERROR=" << conditioning << '\n';
    // Same existing arithmetic metric gate; no new production science budget.
    const double rz_conditioning = RzMetricCases::conditioning_error();
    if (!std::isfinite(rz_conditioning) || rz_conditioning > 2.e-12)
        throw std::runtime_error("independent full-rotation RZ measures");
    std::cout << "RZ_METRIC_MAX_RELATIVE_ERROR=" << rz_conditioning << '\n';
    for (const auto& sample : RzMetricCases::cases) {
        const double dr = sample.upper-sample.lower;
        if (Rz::PhysicalSpacing(0,dr,sample.dz)!=dr
            || Rz::PhysicalSpacing(1,dr,sample.dz)!=sample.dz)
            throw std::runtime_error("RZ physical spacing changed");
    }
    for (double left : {0.,1.,4.}) {
        const double right=left+.25, dz=.5;
        const double volume=Rz::CellVolume(left,right,dz);
        const double radial_lower=Rz::RadialFaceArea(left,dz);
        const double radial_upper=Rz::RadialFaceArea(right,dz);
        const double axial=Rz::AxialFaceArea(left,right);
        // Independent div(r e_r + z e_z)=3; full rotating face fluxes.
        close((right*radial_upper-left*radial_lower+dz*axial)/volume,
              3.,"RZ linear vector divergence");
        close((radial_upper-radial_lower)/volume,
              2./(left+right),"RZ volume-average inverse radius");
        const double middle=.5*(left+right);
        const double fine=Rz::CellVolume(left,middle,dz/2.)
            +Rz::CellVolume(middle,right,dz/2.);
        close(2.*fine/volume,1.,"RZ finite source partition");
        close((Rz::AxialFaceArea(left,middle)+Rz::AxialFaceArea(middle,right))/axial,
              1.,"RZ coarse/fine axial area sum");
    }
    for (double left : {0.,1.,4.}) {
        const double right=left+.25, dz=.5, dt=.125;
        const double inv=2./(left+right);
        close(Rz::InverseRadiusVolumeAverage(left,right),inv,"RZ axis-cell inverse radius");
        const FluidVector rest{2.,0.,0.,0.,20.};
        FluidVector rest_delta{};
        TimeIntegration::add_rz_geometric_source_cell(rest,nullptr,ConstantEos{},
            left,right,dt,rest_delta);
        const double pressure_flux=dt*5.*(Rz::RadialFaceArea(right,dz)
            -Rz::RadialFaceArea(left,dz))/Rz::CellVolume(left,right,dz);
        close(rest_delta.mom_u-pressure_flux,0.,"RZ constant-pressure axis balance");
        if (rest_delta.rho!=0. || rest_delta.eng!=0.
            || rest_delta.mom_v!=0. || rest_delta.mom_w!=0.)
            throw std::runtime_error("RZ rest source changed unrelated component");
        // Distinct z and phi velocities catch accidental polar/axis mapping.
        const FluidVector moving{2.,6.,14.,10.,20.};
        FluidVector delta{17.,19.,23.,29.,31.};
        TimeIntegration::add_rz_geometric_source_cell(moving,nullptr,ConstantEos{},
            left,right,dt,delta);
        close(delta.mom_u-19.,dt*(2.*25.+5.)*inv,"RZ centrifugal source");
        close(delta.mom_w-29.,-dt*2.*3.*5.*inv,"RZ phi curvature source");
        if (delta.mom_v!=23. || delta.rho!=17. || delta.eng!=31.)
            throw std::runtime_error("RZ source changed z/mass/energy");
        auto translated=moving;
        translated.mom_v=-1000.;
        FluidVector other{17.,19.,23.,29.,31.};
        TimeIntegration::add_rz_geometric_source_cell(translated,nullptr,ConstantEos{},
            left,right,dt,other);
        if (delta.mom_u!=other.mom_u || delta.mom_w!=other.mom_w)
            throw std::runtime_error("RZ geometric source depends on axial velocity");
        GeometryView full{};
        full.geometry=Geometry::Cylindrical;full.dim=3;full.x1_min=left;
        full.dx1=right-left;full.dx2=dz;full.dx3=.3;
        FluidVector existing{17.,19.,23.,29.,31.};
        TimeIntegration::add_geometric_source_cell(moving,nullptr,ConstantEos{},
            full,0,0,dt,existing);
        if (existing.mom_u!=delta.mom_u || existing.mom_w!=delta.mom_w
            || existing.mom_v!=delta.mom_v)
            throw std::runtime_error("RZ and full cylindrical source diverged");
    }
    // Preserve the pre-extraction polar/full cylindrical formulas exactly.
    for (int dim : {1,2,3}) {
        GeometryView grid{};
        grid.geometry=Geometry::Cylindrical;grid.dim=dim;
        grid.x1_min=1.;grid.dx1=.25;grid.dx2=.5;grid.dx3=.3;
        const FluidVector u{2.,6.,14.,10.,20.};
        FluidVector actual{17.,19.,23.,29.,31.}, expected=actual;
        const double rho=u.rho,vr=u.mom_u/rho;
        const double vp=dim==2?u.mom_v/rho:(dim==3?u.mom_w/rho:0.);
        const double inv=(1.25-1.)/(.5*(1.25-1.)*(1.25+1.));
        expected.mom_u += .125*(rho*vp*vp+5.)*inv;
        if (dim==2) expected.mom_v += .125*(-rho*vr*(u.mom_v/rho))*inv;
        if (dim==3) expected.mom_w += .125*(-rho*vr*(u.mom_w/rho))*inv;
        TimeIntegration::add_geometric_source_cell(u,nullptr,ConstantEos{},
            grid,0,0,.125,actual);
        if (actual.rho!=expected.rho || actual.eng!=expected.eng
            || actual.mom_u!=expected.mom_u || actual.mom_v!=expected.mom_v
            || actual.mom_w!=expected.mom_w)
            throw std::runtime_error("Legacy cylindrical source formula changed");
    }
    for (double left : {0.,1.,4.}) {
        const auto legacy=make_geometry_view(Geometry::Cylindrical,2,
            {left,-2.,0.},{.25,.5,0.});
        const auto rz=make_rz_geometry_view(legacy);
        if (legacy.semantics!=GeometrySemantics::Existing)
            throw std::runtime_error("RZ conversion changed input view");
        if (CellVolume(rz,0,0,0)!=Rz::CellVolume(left,left+.25,.5)
            || FaceArea(rz,0,0,0,0,false)!=Rz::RadialFaceArea(left,.5)
            || FaceArea(rz,0,0,0,0,true)!=Rz::RadialFaceArea(left+.25,.5)
            || FaceArea(rz,1,0,0,0,false)!=Rz::AxialFaceArea(left,left+.25)
            || FaceArea(rz,1,0,0,0,true)!=Rz::AxialFaceArea(left,left+.25)
            || PhysicalSpacing(rz,0,0,0)!=.25 || PhysicalSpacing(rz,1,0,0)!=.5)
            throw std::runtime_error("RZ view did not consume shared full-ring measures");
        if (PhysicalPosition(rz,{left+.125,-1.75,0.})
                !=std::array<double,3>{left+.125,0.,-1.75})
            throw std::runtime_error("RZ representative position lost axial coordinate");
        FluidVector direct{},through_view{};
        const FluidVector u{2.,6.,14.,10.,100.};
        TimeIntegration::add_rz_geometric_source_cell(u,nullptr,ConstantEos{},
            left,left+.25,.125,direct);
        TimeIntegration::add_geometric_source_cell(u,nullptr,ConstantEos{},
            rz,0,0,.125,through_view);
        if (direct.mom_u!=through_view.mom_u || direct.mom_w!=through_view.mom_w
            || through_view.mom_v!=0. || through_view.rho!=0. || through_view.eng!=0.)
            throw std::runtime_error("RZ shared view source mapping drifted");
    }
    for (Geometry kind : {Geometry::Cartesian,Geometry::Spherical}) {
        bool rejected=false;
        try { (void)make_rz_geometry_view(make_geometry_view(kind,2,{0.,0.,0.},{1.,1.,0.})); }
        catch (const std::invalid_argument&) {rejected=true;}
        if (!rejected) throw std::runtime_error("RZ view accepted unrelated geometry");
    }
    for (int dimension : {1,3}) {
        bool rejected=false;
        try { (void)make_rz_geometry_view(make_geometry_view(
            Geometry::Cylindrical,dimension,{0.,0.,0.},{1.,1.,1.})); }
        catch (const std::invalid_argument&) {rejected=true;}
        if (!rejected) throw std::runtime_error("RZ view accepted unsupported dimension");
    }
    const double pi = arch::constants::math::pi;
    for (Geometry geometry : {Geometry::Cartesian, Geometry::Cylindrical, Geometry::Spherical})
    for (int dimension : {1, 2, 3})
    for (double radius : {0.0, 1.0, 10.0})
    for (double theta_left : {0.0, pi/3, pi/2, 5*pi/6}) {
        GeometryView grid{};
        grid.geometry = geometry; grid.dim = dimension;
        grid.dx1 = .25; grid.dx2 = pi/6; grid.dx3 = .2;
        grid.x1_min = radius; grid.x2_min = theta_left;
        const double theta = theta_left + pi/12;
        const double r = radius + .125;
        double width_y = grid.dx2, width_z = grid.dx3;
        if (geometry != Geometry::Cartesian) {
            if (dimension == 2 || geometry == Geometry::Spherical) width_y *= r;
            if (dimension == 3) width_z *= r;
            if (dimension == 3 && geometry == Geometry::Spherical) width_z *= std::sin(theta);
        }
        close(PhysicalSpacing(grid, 0, 0, 0), .25, "radial length");
        if (dimension >= 2) close(PhysicalSpacing(grid, 1, 0, 0), width_y, "second length");
        if (dimension == 3) close(PhysicalSpacing(grid, 2, 0, 0), width_z, "third length");
        const FluidVector state{1, 0, 0, 0, 20};
        double inverse_dt = 2/.25;
        if (dimension >= 2) inverse_dt += 2/width_y;
        if (dimension == 3) inverse_dt += 2/width_z;
        close(evaluate_cfl_cell_dt(state, nullptr, ConstantEos{}, grid, 0, 0),
              0.5/inverse_dt, "two-face convex CFL");

        // Independent rest-state balance: fluxes have constant pressure on
        // normal momentum faces, no mass/energy flux. It must cancel sources.
        FluidVector source{};
        TimeIntegration::add_geometric_source_cell(state, nullptr, ConstantEos{}, grid, 0, 0, 1, source);
        const double sources[]{source.mom_u, source.mom_v, source.mom_w};
        for (int direction = 0; direction < dimension; ++direction) {
            const double divergence = 5.0 * (FaceArea(grid, direction, 0, 0, 0, true)
                - FaceArea(grid, direction, 0, 0, 0, false)) / CellVolume(grid, 0, 0, 0);
            close(sources[direction] - divergence, 0, "constant-pressure equilibrium");
        }
        if (geometry == Geometry::Spherical && dimension == 3) {
            const double integral_r = (std::pow(radius + .25, 2) - radius * radius) / 2;
            close(FaceArea(grid, 1, 0, 0, 0, true), integral_r * std::sin(theta_left + pi/6) * .2,
                  "theta physical area");
            close(FaceArea(grid, 2, 0, 0, 0, false), integral_r * pi/6,
                  "phi physical area");
        }
    }
    std::cout << "CURVILINEAR_METRICS_PASS\n";
    SpeciesManager species;
    species.add_species("gas", 1., 1., 1.4, 3.);
    IdealGas eos(1.4, species);
    SimConfig config{};
    config.physics.diffusion.use_diffusion = true;
    config.physics.diffusion.use_viscous_diffusion = true;
    config.physics.diffusion.nu_visc = ViscousGeometryCases::viscosity;
    const auto evaluate = [&](const FluidState& state, const Grid& grid) {
        FluidState delta;
        delta.Preallocate(grid.GetTotalSize()); delta.InitSpecies(1);
        DiffFlux::compute_diffusion_operator(state, delta, eos, grid, config);
        ViscousGeometryCases::Evaluation result{};
        result.derivative.resize(grid.GetTotalSize());
        for (int cell=0; cell<grid.GetTotalSize(); ++cell) result.derivative[cell] = delta.get(cell);
        result.raw_dt = DiffFlux::adaptive_dt_diff(state, eos, grid, config, 1.);
        return result;
    };
    ViscousGeometryCases::convergence("cpu", evaluate);
    ViscousGeometryCases::radial_origin("cpu", evaluate);
    ViscousGeometryCases::density_stability("cpu", evaluate);
}
