/**
 * @file test_curvilinear_metrics.cpp
 * @brief Check shared geometry and source terms against analytic measures.
 *
 * The cases span Cartesian, cylindrical and spherical coordinates, including
 * thin shells, poles and radial-origin behavior.
 */
#include "physics/boundary/BoundaryDiagnostics.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/DriverUtils.h"
#include "numerics/curvilinear/GeometricSources.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "numerics/flux/FluxHLLC.h"
#include "math/geometry/CurvilinearMetricCases.h"
#include "math/geometry/ViscousGeometryCases.h"
#include "math/geometry/RadialStokesCases.h"
#include "math/geometry/CurvedStokesCases.h"
#include "numerics/diffusion/DiffFlux.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/ExternalGravity.h"
#include "physics/diagnostics/VelocityDiagnostics.h"
#include <iostream>
#include <cfenv>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <vector>
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

/** Actual canonical single-block Cartesian BC -> diffusion -> FE work witnesses.
 * The Grid/BCHandler contract fixes 16 cells per active axis (not a fabricated
 * 8-cell layout). Active data alone are initialized; apply() fills every real
 * ghost/corner. Independent source-index/parity checks precede true operator
 * and dt calls. Integral references use constant Cartesian V/A, not the stress
 * implementation. This is Host closed-boundary qualification, not AMR/RKL.
 */
void test_cartesian_real_diffusion_boundaries()
{
    SpeciesManager species;species.add_species("wall-gas",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    constexpr double pi=3.141592653589793238462643383279502884;
    for(int dimension:{1,2,3})for(bool reflecting:{false,true})
    for(bool translation:{false,true}) {
        if(reflecting&&translation)continue; // translation is a periodic null.
        SimConfig config{};config.grid.dim=dimension;config.grid.geometry="cartesian";
        config.grid.x1_min=0.;config.grid.x1_max=1.;config.grid.x2_min=0.;config.grid.x2_max=1.;
        config.grid.x3_min=0.;config.grid.x3_max=1.;
        const std::string token=reflecting?"reflecting":"periodic";
        config.grid.x1l_boundary_type=token;config.grid.x1r_boundary_type=token;
        config.grid.x2l_boundary_type=token;config.grid.x2r_boundary_type=token;
        config.grid.x3l_boundary_type=token;config.grid.x3r_boundary_type=token;
        config.physics.diffusion.use_diffusion=true;config.physics.diffusion.use_viscous_diffusion=true;
        config.physics.diffusion.use_thermal_diffusion=false;config.physics.diffusion.use_species_diffusion=false;
        config.physics.diffusion.nu_visc=ViscousGeometryCases::viscosity;
        Grid grid(amr::MAX_NG,0.,1.,0.,1.,0.,1.);grid.dim=dimension;grid.InitializeTopology();
        FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
        for(int k=grid.Ks();k<grid.Ke();++k)for(int j=grid.Js();j<grid.Je();++j)
        for(int i=grid.Is();i<grid.Ie();++i) {
            const int cell=grid.GetIndex(i,j,k);
            const double x=grid.GetCellCenterX(i),y=dimension>=2?grid.GetCellCenterY(j):0.,
                z=dimension==3?grid.GetCellCenterZ(k):0.;
            // Positive varying dyadic rho makes the translation's velocity
            // divisions exact; no constant-density exemption is needed.
            const double rho=1.+.25*((i-grid.Is())%2)+.125*((j-grid.Js())%2)+.0625*((k-grid.Ks())%2);
            std::array<double,3> u{.25,-.375,.5};
            if(!translation) {
                if(reflecting) {
                    u[0]=.2*std::sin(pi*x)*(1.+.3*std::cos(pi*y)+.2*std::cos(pi*z));
                    u[1]=dimension>=2?.3*std::sin(pi*y)*(1.+.2*std::cos(pi*x)+.1*std::cos(pi*z)):.3*std::cos(pi*x);
                    u[2]=dimension==3?.4*std::sin(pi*z)*(1.+.15*std::cos(pi*x)+.1*std::cos(pi*y)):
                        .4*std::cos(pi*x)*std::cos(pi*y);
                } else {
                    u[0]=.3*std::sin(2.*pi*x)+.1*std::cos(2.*pi*y);
                    u[1]=.2*std::cos(2.*pi*x)+.25*std::sin(2.*pi*y);
                    u[2]=.15*std::sin(2.*pi*x)+.2*std::cos(2.*pi*y)+.25*std::sin(2.*pi*z);
                }
            }
            state.set(cell,{rho,rho*u[0],rho*u[1],rho*u[2],rho*(30.+.5*(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]))});
            state.X(0,cell)=1.;state.enuc_rate[cell]=0.;
        }
        const FluidState active_seed=state;
        BCHandler boundary(config);boundary.bind(eos,species);
        boundary.configure_stage(.125,arch::boundary::BoundaryPurpose::Diffusion);
        boundary.apply(state,grid);
        const int lower[3]{grid.Is(),grid.Js(),grid.Ks()},upper[3]{grid.Ie(),grid.Je(),grid.Ke()};
        for(int k=0;k<grid.GetTotalZ();++k)for(int j=0;j<grid.GetTotalY();++j)
        for(int i=0;i<grid.GetTotalX();++i) {
            const int position[3]{i,j,k};int source[3]{i,j,k};double signs[3]{1.,1.,1.};
            bool ghost=false;
            for(int axis=0;axis<dimension;++axis) {
                const int extent=upper[axis]-lower[axis];
                if(position[axis]<lower[axis]) {
                    ghost=true;source[axis]=reflecting?2*lower[axis]-position[axis]-1:position[axis]+extent;
                    if(reflecting)signs[axis]=-1.;
                } else if(position[axis]>=upper[axis]) {
                    ghost=true;source[axis]=reflecting?2*upper[axis]-position[axis]-1:position[axis]-extent;
                    if(reflecting)signs[axis]=-1.;
                }
            }
            const int cell=grid.GetIndex(i,j,k),donor=grid.GetIndex(source[0],source[1],source[2]);
            const auto actual=state.get(cell),expected=active_seed.get(donor);
            if(!std::isfinite(actual.rho)||!(actual.rho>0.)||actual.rho!=expected.rho||
               actual.mom_u!=signs[0]*expected.mom_u||actual.mom_v!=signs[1]*expected.mom_v||
               actual.mom_w!=signs[2]*expected.mom_w||actual.eng!=expected.eng||
               state.X(0,cell)!=active_seed.X(0,donor)||state.enuc_rate[cell]!=active_seed.enuc_rate[donor])
                throw std::runtime_error(ghost?"Cartesian actual ghost/corner parity mismatch":"Cartesian boundary overwrote active source");
        }
        state.boundary_flux_capture=std::make_shared<arch::boundary::BoundaryFluxCaptureStorage>();
        for(int axis=0;axis<dimension;++axis) {
            const int a=(axis+1)%3,b=(axis+2)%3;
            const std::size_t count=static_cast<std::size_t>(upper[a]-lower[a])*(upper[b]-lower[b])*7;
            for(int side=0;side<2;++side)state.boundary_flux_capture->stage[2*axis+side].assign(count,0.);
        }
        const FluidState before=state;
        FluidState delta;delta.Preallocate(grid.GetTotalSize());delta.InitSpecies(1);
        const double dt=DiffFlux::adaptive_dt_diff(state,eos,grid,config,1.);
        if(!std::isfinite(dt)||!(dt>0.))throw std::runtime_error("Cartesian real-BC dt rejected");
        DiffFlux::compute_diffusion_operator(state,delta,eos,grid,config);
        // Const operator may update only the explicit observer, never source.
        if(state.rho!=before.rho||state.mom_u!=before.mom_u||state.mom_v!=before.mom_v||
           state.mom_w!=before.mom_w||state.eng!=before.eng||state.mass_fractions!=before.mass_fractions||
           state.enuc_rate!=before.enuc_rate)throw std::runtime_error("Cartesian diffusion mutated its actual source");
        const double h[3]{grid.dx1,grid.dx2,grid.dx3};
        long double volume=1.;for(int axis=0;axis<dimension;++axis)volume*=h[axis];
        long double ke=0.,ke_next=0.,dke=0.,energy=0.,energy_scale=0.,work_scale=0.,boundary_energy=0.,boundary_scale=0.;
        double null_error=0.;
        for(int k=grid.Ks();k<grid.Ke();++k)for(int j=grid.Js();j<grid.Je();++j)
        for(int i=grid.Is();i<grid.Ie();++i) {
            const int cell=grid.GetIndex(i,j,k);const auto u=state.get(cell),d=delta.get(cell);
            if(d.rho!=0.||delta.X(0,cell)!=0.||!std::isfinite(d.eng))throw std::runtime_error("Cartesian viscosity changed mass/species or produced invalid energy");
            const double momentum[3]{u.mom_u,u.mom_v,u.mom_w},increment[3]{d.mom_u,d.mom_v,d.mom_w};
            for(int n=0;n<3;++n) {
                if(!std::isfinite(increment[n]))throw std::runtime_error("Cartesian viscous momentum is nonfinite");
                const long double term=volume*momentum[n]*increment[n]/u.rho;
                dke+=term;work_scale+=std::abs(term);
                ke+=volume*momentum[n]*momentum[n]/(2.L*u.rho);
                const long double next=static_cast<long double>(momentum[n])+dt*increment[n];
                ke_next+=volume*next*next/(2.L*u.rho);
                null_error=std::max(null_error,std::abs(increment[n]));
            }
            energy+=volume*d.eng;energy_scale+=std::abs(volume*d.eng);
            null_error=std::max(null_error,std::abs(d.eng));
        }
        for(int axis=0;axis<dimension;++axis)for(int side=0;side<2;++side) {
            const auto& plane=state.boundary_flux_capture->stage[2*axis+side];
            for(std::size_t offset=0;offset<plane.size();offset+=7) {
                for(int field=0;field<7;++field)if(!std::isfinite(plane[offset+field]))
                    throw std::runtime_error("Cartesian surface observer published nonfinite flux");
                const long double term=(side?1.L:-1.L)*(volume/h[axis])*plane[offset+4];
                boundary_energy+=term;boundary_scale+=std::abs(term);
                if(plane[offset]!=0.||plane[offset+5]!=0.)throw std::runtime_error("Cartesian wall has viscous mass/species flux");
            }
        }
        constexpr long double tolerance=2.e-12L;
        if(dke>tolerance*std::max(1.L,work_scale))throw std::runtime_error("Cartesian actual-BC kinetic derivative is positive");
        if(ke_next-ke>tolerance*std::max(1.L,ke))throw std::runtime_error("Cartesian actual recommended FE dt increases kinetic energy");
        if(std::abs(boundary_energy)>tolerance*std::max(1.L,boundary_scale)||
           std::abs(energy+boundary_energy)>tolerance*std::max(1.L,energy_scale+boundary_scale))
            throw std::runtime_error("Cartesian closed real-boundary energy accounting failed");
        if(translation&&null_error>2.e-12)throw std::runtime_error("Cartesian variable-rho periodic translation is not null");
        std::cout<<"CARTESIAN_REAL_BC_WORK dim="<<dimension<<" boundary="<<token<<" translation="<<translation
            <<" dt="<<dt<<" dke="<<static_cast<double>(dke)<<" energy="<<static_cast<double>(energy)
            <<" boundary_energy="<<static_cast<double>(boundary_energy)<<" fe_delta_ke="<<static_cast<double>(ke_next-ke)<<'\n';
    }
}

} // namespace

int main(int argc,char** argv)
{
    using namespace GridMetrics;
    {
        GeometryView retired{};retired.geometry=Geometry::Cylindrical;retired.dim=2;
        retired.dx1=retired.dx2=retired.dx3=1.;retired.x1_min=1.;
        if(resolve_coordinate_layout(retired.geometry,retired.dim).valid
           ||std::isfinite(CellVolume(retired,0,0,0))
           ||std::isfinite(FaceArea(retired,0,0,0,0,false))
           ||std::isfinite(PhysicalSpacing(retired,0,0,0)))
            throw std::runtime_error("retired cylindrical two-dimensional raw layout/metrics accepted");
        Grid grid(amr::MAX_NG,1.,2.,-.5,.5,0.,1.);grid.dim=2;grid.geometry="cylindrical";
        bool grid_rejected=false,coordinates_rejected=false;
        try {grid.InitializeTopology();}catch(const std::invalid_argument&) {grid_rejected=true;}
        try {(void)Grid::PhysicalCoordsFromNative(2,"cylindrical",1.5,0.,0.);}
        catch(const std::invalid_argument&) {coordinates_rejected=true;}
        if(!grid_rejected||!coordinates_rejected)
            throw std::runtime_error("retired cylindrical two-dimensional Host grid/coordinates accepted");
    }
    const double conditioning = CurvilinearMetricCases::conditioning_error();
    if (!std::isfinite(conditioning) || conditioning > 2.e-12)
        throw std::runtime_error("independent thin-shell/polar measures");
    std::cout << "INDEPENDENT_METRIC_MAX_RELATIVE_ERROR=" << conditioning << '\n';
    // Same existing arithmetic metric gate; no new production science budget.
    const double pi = arch::constants::math::pi;
    for (Geometry geometry : {Geometry::Cartesian, Geometry::Cylindrical, Geometry::Spherical})
    for (int dimension : {1, 2, 3})
    for (double radius : {0.0, 1.0, 10.0})
    for (double theta_left : {0.0, pi/3, pi/2, 5*pi/6}) {
        // Two-dimensional cylindrical support is retired; this ordinary metric
        // matrix enumerates the retained Cartesian, spherical and one-/three-
        // dimensional cylindrical charts only.
        if (geometry == Geometry::Cylindrical && dimension == 2) continue;
        GeometryView grid{};
        grid.geometry = geometry; grid.dim = dimension;
        grid.dx1 = .25; grid.dx2 = pi/6; grid.dx3 = .2;
        grid.x1_min = radius; grid.x2_min = theta_left;
        const double theta = theta_left + pi/12;
        const double r = radius + .125;
        double width_y = grid.dx2, width_z = grid.dx3;
        if (geometry != Geometry::Cartesian) {
            if (geometry == Geometry::Spherical) width_y *= r;
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
    ViscousGeometryCases::newtonian_constitutive();
    ViscousGeometryCases::newtonian_covariant_gradient();
    ViscousGeometryCases::newtonian_paired_faces();
    test_cartesian_real_diffusion_boundaries();
    ViscousGeometryCases::convergence("cpu", evaluate);
    ViscousGeometryCases::radial_origin("cpu", evaluate);
    ViscousGeometryCases::density_stability("cpu", evaluate);
    RadialStokesCases::density_energy("cpu",ViscousGeometryCases::viscosity,evaluate);
    CurvedStokesCases::density_energy("cpu",ViscousGeometryCases::viscosity,evaluate);
    CurvedStokesCases::cylindrical_axis_energy("cpu",ViscousGeometryCases::viscosity,evaluate);
}
