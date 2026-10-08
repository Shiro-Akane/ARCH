/**
 * @file test_curvilinear_metrics.cpp
 * @brief Check shared geometry and source terms against analytic measures.
 *
 * The cases span Cartesian, cylindrical and spherical coordinates, including
 * thin shells, poles and radial-origin behavior.
 */
#include "driver/DriverUtils.h"
#include "numerics/integrator/GeometricSources.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "numerics/reconstruction/RzDensityMoments.h"
#include "numerics/reconstruction/RzSelectedReconstruction.h"
#include "numerics/state/RzNativeClosure.h"
#include "numerics/flux/FluxHLLC.h"
#include "math/geometry/CurvilinearMetricCases.h"
#include "math/geometry/RzMetricCases.h"
#include "math/geometry/ViscousGeometryCases.h"
#include "numerics/diffusion/DiffFlux.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/ExternalGravity.h"
#include "physics/diagnostics/VelocityDiagnostics.h"
#include <iostream>
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

#include "math/geometry/RzEquilibriumCases.h"
#include "math/geometry/RzReconstructionCases.h"
#include "math/geometry/RzViscousCases.h"

namespace {
void close(double actual, double expected, const char* name) {
    if (!std::isfinite(actual) || std::abs(actual - expected) >
        2.e-12 * std::max(1.0, std::abs(expected)))
        throw std::runtime_error(name);
}

/** Independent physical polynomial integral: int r^power dr on [left,right]. */
long double supported_density_power_integral(long double left,long double right,int power)
{
    long double low=1.,high=1.;
    for(int n=0;n<=power;++n) {low*=left;high*=right;}
    return (high-low)/static_cast<long double>(power+1);
}

/** Independent antiderivatives for rho=a+b*r+c*r^2; no production moments/Gauss.
 * V mass is int rho*|r| dr; signed capacity is int rho*r^3 dr and physical
 * rotational inertia is 2*pi*dz*abs(capacity), including reflected ghosts.
 */
struct SupportedDensityReference {
    long double a,b,c;
    long double integral(long double left,long double right,int power) const {
        return a*supported_density_power_integral(left,right,power)
            +b*supported_density_power_integral(left,right,power+1)
            +c*supported_density_power_integral(left,right,power+2);
    }
    long double mass(long double left,long double right) const {
        return (right<=0.?-1.L:1.L)*integral(left,right,1);
    }
    double mean(double left,double right) const {
        const long double area=(right<=0.?-1.L:1.L)*
            supported_density_power_integral(left,right,1);
        return static_cast<double>(mass(left,right)/area);
    }
    double point(double r) const {return static_cast<double>(a+r*(b+r*c));}
};

/** A real finite row range: count only the three advertised reads in order.
 * Everything outside the support, and each non-density field, is poisoned.
 */
struct SupportedDensityReader {
    const std::vector<FluidVector>& cells;
    int first;
    mutable std::array<int,3> observed{};
    mutable int count=0;
    FluidVector operator()(int index) const {
        if(index<first||index>first+2||index<0||index>=static_cast<int>(cells.size())||count>=3)
            throw std::runtime_error("RZ supported density accessed unavailable real support");
        observed[count++]=index;
        return cells[index];
    }
    void require_three() const {
        if(count!=3||observed!=std::array<int,3>{first,first+1,first+2})
            throw std::runtime_error("RZ supported density changed its real three-cell read order");
    }
};

/** Check actual supported closure against independent physical antiderivatives.
 * Targets include the outermost real column and signed reflected axis ghosts.
 * The original 2e-12*max(1,abs(reference)) owner budget remains unchanged.
 */
void test_rz_supported_density()
{
    const double nan=std::numeric_limits<double>::quiet_NaN();
    auto grid_for=[](int ng,int columns) {
        GridMetrics::GeometryView grid;
        grid.geometry=GridMetrics::Geometry::Cylindrical;
        grid.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
        grid.dim=2;grid.ng=ng;grid.stride_y=columns;
        grid.total_size=3*columns;grid.dx1=1.;grid.x1_min=0.;
        return grid;
    };
    const auto check=[&](const GridMetrics::GeometryView& grid,int support_begin,
        const SupportedDensityReference& reference) {
        const int row_begin=grid.stride_y; // A real nonfirst row, not a flattened halo.
        std::vector<FluidVector> cells(grid.total_size,FluidVector{nan,nan,nan,nan,nan});
        for(int offset=0;offset<3;++offset) {
            const int column=support_begin+offset;
            cells[row_begin+column].rho=reference.mean(grid.GetFacePosL(column),grid.GetFacePosR(column));
        }
        for(int offset=0;offset<3;++offset) {
            const int i=support_begin+offset,index=row_begin+i;
            const SupportedDensityReader read{cells,row_begin+support_begin};
            const auto cell=RzDensity::density_cell_supported(read,index,grid,i,support_begin);
            read.require_three();
            if(!cell.valid)throw std::runtime_error("RZ real supported polynomial was rejected");
            const double left=grid.GetFacePosL(i),right=grid.GetFacePosR(i);
            close(cell.lower,left,"RZ supported lower bound");
            close(cell.upper,right,"RZ supported upper bound");
            close(cell.origin,grid.GetCellCenterX(i),"RZ supported target origin");
            close(cell.spacing,grid.dx1,"RZ supported spacing");
            const long double capacity=reference.integral(left,right,3);
            close(cell.capacity,static_cast<double>(capacity),"RZ supported signed C antiderivative");
            close(cell.mean_s,static_cast<double>(reference.integral(left,right,5)/capacity),
                "RZ supported r-square abscissa antiderivative");
            for(double radius:{left,.5*(left+right),right})
                close(cell.density.at((radius-cell.origin)/cell.spacing),reference.point(radius),
                    "RZ supported physical density polynomial");
            // Translate the RESULT polynomial for an independent physical integral.
            const long double h=cell.spacing,o=cell.origin,
                c=static_cast<long double>(cell.density.quadratic)/(h*h),
                b=static_cast<long double>(cell.density.linear)/h-2.L*c*o,
                a=cell.density.constant-static_cast<long double>(cell.density.linear)*o/h+c*o*o;
            const SupportedDensityReference reconstructed{a,b,c};
            close(reconstructed.mean(left,right),cells[index].rho,"RZ supported target V mean");
            constexpr long double pi=3.141592653589793238462643383279502884L,dz=.75L;
            const long double ring=2.L*pi*dz;
            close(static_cast<double>(ring*reconstructed.mass(left,right)),
                static_cast<double>(ring*reference.mass(left,right)),"RZ supported physical M");
            close(static_cast<double>(ring*std::abs(static_cast<long double>(cell.capacity))),
                static_cast<double>(ring*std::abs(capacity)),"RZ supported physical I");
            if(offset==1) {
                // API compatibility only; independent physical integrals above are the oracle.
                const SupportedDensityReader centered_read{cells,row_begin+support_begin};
                const auto centered=RzDensity::density_cell(centered_read,index,grid,i);
                centered_read.require_three();
                const auto fields=[](const RzDensity::Cell& value) {
                    return std::array<double,13>{value.density.constant,value.density.linear,
                        value.density.quadratic,value.origin,value.spacing,value.lower,value.upper,
                        value.capacity,value.mean_s,value.density_scale,value.radius_scale,
                        value.weighted_two,value.weighted_three};
                };
                const auto original=fields(centered),supported=fields(cell);
                if(centered.valid!=cell.valid)throw std::runtime_error("RZ central validity changed");
                for(std::size_t n=0;n<original.size();++n)
                    if(std::bit_cast<std::uint64_t>(original[n])!=std::bit_cast<std::uint64_t>(supported[n]))
                        throw std::runtime_error("RZ centered density arithmetic path changed");
            }
        }
    };
    // Positive outer ghost [3,4] sees only [1,2],[2,3],[3,4]; no fourth cell exists.
    check(grid_for(1,5),2,{1.25L,0.L,0.L});
    check(grid_for(1,5),2,{2.L,.125L,.25L});
    // Even density across distinct cells touching the axis remains valid; a cell
    // that itself straddles the axis is rejected by the separate negative below.
    check(grid_for(3,9),0,{1.25L,0.L,0.L});
    check(grid_for(3,9),0,{2.L,0.L,.25L});
    check(grid_for(1,5),0,{1.25L,0.L,0.L});
    check(grid_for(1,5),0,{2.L,0.L,.25L});

    {
        auto geometry=grid_for(1,3);geometry.x1_min=1.;
        std::vector<FluidVector> cells(geometry.total_size,FluidVector{nan,nan,nan,nan,nan});
        cells[3].rho=1.;cells[4].rho=1.;cells[5].rho=100.;
        const SupportedDensityReader read{cells,3};
        const auto cell=RzDensity::density_cell_supported(read,3,geometry,0,0);
        read.require_three();
        // These positive means on [0,1],[1,2],[2,3] fit the physical
        // polynomial 56-123.75*r+55*r^2, whose rho(1)=-12.75 requires the
        // existing positivity contraction. Its authoritative target mean is 1.
        if(!cell.valid)throw std::runtime_error("RZ one-sided positive mean contraction failed");
        const long double h=cell.spacing,o=cell.origin,
            c=static_cast<long double>(cell.density.quadratic)/(h*h),
            b=static_cast<long double>(cell.density.linear)/h-2.L*c*o,
            a=cell.density.constant-static_cast<long double>(cell.density.linear)*o/h+c*o*o;
        const SupportedDensityReference contracted{a,b,c};
        close(contracted.mean(0.,1.),1.,"RZ one-sided contraction preserved actual target V mean");
        close(cell.capacity,static_cast<double>(contracted.integral(0.,1.,3)),
            "RZ one-sided contracted signed C");
        close(cell.mean_s,static_cast<double>(contracted.integral(0.,1.,5)/contracted.integral(0.,1.,3)),
            "RZ one-sided contracted graph abscissa");
    }

    const auto grid=grid_for(1,5);
    std::vector<FluidVector> cells(grid.total_size,FluidVector{1.,nan,nan,nan,nan});
    auto reject_without_read=[&](const GridMetrics::GeometryView& geometry,int index,int i,int begin) {
        const SupportedDensityReader read{cells,7};
        if(RzDensity::density_cell_supported(read,index,geometry,i,begin).valid||read.count!=0)
            throw std::runtime_error("RZ invalid real geometry/support was read or accepted");
    };
    reject_without_read(grid,6,1,2); // Target not in advertised support.
    reject_without_read(grid,8,3,-1);
    reject_without_read(grid,8,3,3); // Would borrow the following row.
    reject_without_read(grid,8,2,2); // Flat index does not name target column.
    reject_without_read(grid,-1,3,2);
    reject_without_read(grid,grid.total_size,3,2);
    auto invalid=grid;invalid.total_size=9;
    reject_without_read(invalid,8,3,2); // Third real support index 9 is missing.
    invalid=grid;invalid.x1_min=.25;
    reject_without_read(invalid,7,2,0); // Actual [-.75,.25] support cell straddles axis.
    for(double spacing:{0.,-1.,nan,std::numeric_limits<double>::infinity()}) {
        invalid=grid;invalid.dx1=spacing;reject_without_read(invalid,8,3,2);
    }
    invalid=grid;invalid.x1_min=nan;reject_without_read(invalid,8,3,2);
    invalid=grid;invalid.semantics=GridMetrics::GeometrySemantics::Existing;
    reject_without_read(invalid,8,3,2);
    invalid=grid;invalid.geometry=GridMetrics::Geometry::Cartesian;
    reject_without_read(invalid,8,3,2);
    invalid=grid;invalid.dim=1;reject_without_read(invalid,8,3,2);
    invalid=grid;invalid.ng=0;reject_without_read(invalid,8,3,2);
    invalid=grid;invalid.stride_y=2;reject_without_read(invalid,1,1,0);
    invalid=grid;invalid.total_size=0;reject_without_read(invalid,0,0,0);
    for(int offset=0;offset<3;++offset)
        for(double rho:{0.,-1.,nan,std::numeric_limits<double>::infinity()}) {
            cells[7+offset].rho=rho;
            const SupportedDensityReader read{cells,7};
            if(RzDensity::density_cell_supported(read,8,grid,3,2).valid)
                throw std::runtime_error("RZ nonpositive/nonfinite real support density accepted");
            read.require_three();cells[7+offset].rho=1.;
        }
}

struct ConstantEos {
    double get_pressure(const FluidVector&, const double*) const { return 5.0; }
    double get_sound_speed(const FluidVector&, double, const double*) const { return 2.0; }
};

/** Attach actual EOS acceptance to the original manual geometry-unit scheduler.
 * This adapter preserves the fixture's independent handles/ledger/clock and
 * spatial samples. It is not the authenticated production Runtime service.
 * The gate sees actual post-BC/exchange slot buffers, preflights exact Host
 * interiors for the entire domain, then validates their density closure and
 * physical baseline using the original IdealGas and configured bounds.
 */
void bind_rz_geometry_fixture_acceptance(
    arch::scheduler::StageExecutionContext& context,amr::AMRControl& control,
    const std::vector<amr::BlockHandle>& handles,const IdealGas& eos,
    const NumericsConfig& numerics,int& completed_gates)
{
    const auto active=control.tree->GetActiveBlocks();
    const arch::state::Bounds bounds{
        numerics.sml_rho,numerics.min_eint,numerics.max_eint};
    if(active.empty()||handles.size()!=active.size()||!arch::state::valid_bounds(bounds))
        throw std::logic_error("RZ geometry fixture acceptance has an invalid domain or bounds");
    auto* const expected_context=&context;
    auto* const expected_ledger=&context.ledger;
    auto* const expected_clock=&context.clock;
    const auto epoch=context.ledger.active_epoch();
    const int species=control.pool->GetBlock(active.front()).fluid_state.GetNumSpecies();
    const std::span<const amr::BlockHandle> borrowed_handles=handles;
    context.post_boundary_acceptance=[&control,&eos,&completed_gates,
        expected_context,expected_ledger,expected_clock,epoch,species,bounds,
        active,borrowed_handles,frozen_handles=handles](
            const arch::scheduler::StageExecutionContext& actual,
            arch::state::StateSlot slot,arch::state::StateVersion version) {
        const auto member=TimeIntegration::hydro_boundary_state_member(slot);
        const auto require_frame=[&] {
            const auto& binding=arch::scheduler::current_stage_binding();
            const auto live_handles=control.ActiveHandles();
            if(&actual!=expected_context||actual.side!=arch::state::ExecutionSide::Host
                ||&actual.ledger!=expected_ledger||&actual.clock!=expected_clock
                ||actual.ledger.active_epoch()!=epoch
                ||control.tree->GetActiveBlocks()!=active
                ||&binding.context!=&actual
                ||binding.handles.data()!=borrowed_handles.data()
                ||binding.handles.size()!=frozen_handles.size()
                ||live_handles.data()!=borrowed_handles.data()
                ||live_handles.size()!=frozen_handles.size()
                ||!std::equal(binding.handles.begin(),binding.handles.end(),frozen_handles.begin()))
                throw std::logic_error("RZ geometry fixture acceptance owner/handle frame changed");
            // Ghost readiness has not been published yet. Require exactly the
            // candidate Host interior version, not the preceding ghost version.
            for(const auto handle:frozen_handles) {
                actual.ledger.require_readable({handle,slot},
                    {arch::state::ExecutionSide::Host,version,true,false});
                const auto state=actual.ledger.inspect({handle,slot});
                if(state.interior.pending_transfer!=arch::state::PendingTransferPhase::None
                    ||state.ghost.pending_transfer!=arch::state::PendingTransferPhase::None)
                    throw std::logic_error("RZ geometry fixture acceptance has a pending transfer");
            }
        };
        require_frame();
        for(int id:active) {
            const auto& block=control.pool->GetBlock(id);
            RzThermodynamics::validate_patch_eos(block.*member,block.grid,species,bounds,eos);
        }
        require_frame();
        ++completed_gates;
    };
}
}


void test_rz_angular_measures() {
    using namespace GridMetrics::Rz;
    constexpr double pi=3.141592653589793238462643383279502884;
    // Independently integrated solid rotation rho=Omega=1, r=[1/2,1], dz=1:
    // J=2*pi*integral(r^3 dr)=15*pi/32; m_phi=J/W=45/56.
    const double v=CellVolume(.5,1.,1.);
    const double w=AngularMomentumMeasure(.5,1.,1.);
    close(v,3.*pi/4.,"RZ angular volume");
    close(w,7.*pi/12.,"RZ W measure");
    close(VolumeCentroidRadius(.5,1.),7./9.,"RZ V centroid");
    close(AngularReconstructionRadius(.5,1.),45./56.,"RZ W centroid");
    close(arch::state::rz_angular_integral(45./56.,w),15.*pi/32.,"RZ rigid rotation J");
    close(arch::state::rz_angular_density(45./56.,w,v),5./8.,"RZ derived ell");
    close(arch::state::rz_representative_azimuthal_velocity(45./56.,1.),
          45./56.,"RZ representative velocity");
    if(RadialTorqueMeasure(0.,1.)!=0.)
        throw std::runtime_error("RZ axis torque not exact zero");
    close(RadialTorqueMeasure(.5,1.),pi/2.,"RZ radial torque measure");
    close(AxialTorqueMeasure(.5,1.),7.*pi/12.,"RZ axial torque measure");
    for(double left : {0.,.5,1.e10}) {
        const double right=left+1.;
        const double mid=left+.5;
        const double parent=AngularMomentumMeasure(left,right,1.);
        const double children=AngularMomentumMeasure(left,mid,.5)
                             +AngularMomentumMeasure(mid,right,.5);
        close(2.*children,parent,"RZ W radial/axial partition");
        const double c=AngularReconstructionRadius(left,right);
        if(!(c>left && c<right))throw std::runtime_error("RZ W centroid outside cell");
    }
    // Frozen inadmissible parent: W weights 1:7, V weights 1:3.
    // This is the arithmetic reference, not an AMR transaction acceptance.
    const double parent_m=(1.-7.*16.)/8.;
    const double parent_e=(9./16.+3.*2049./16.)/4.;
    close(parent_m,-111./8.,"RZ frozen angular parent");
    close(parent_e-.5*parent_m*parent_m,-9./128.,"RZ frozen parent veto reference");
    std::cout<<"RZ_ANGULAR_MEASURES_PASS\n";
}


void test_rz_torque_divergence_budget() {
    constexpr long double pi=3.141592653589793238462643383279502884L;
    constexpr double dt=.001;
    SpeciesManager species;
    species.add_species("gas0",1.,1.,1.4,3.);
    species.add_species("gas1",2.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    for(double inner:{0.,1.})for(int lane:{0,1,2}) {
        Grid grid(amr::MAX_NG,inner,inner+1.,-.5,1.,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology();
        const int size=grid.GetTotalSize();
        FluidState state,updated;
        state.Preallocate(size);state.InitSpecies(2);
        updated.Preallocate(size);updated.InitSpecies(2);
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            const int cell=grid.GetIndex(i,j,0);
            const double left=grid.GetFacePosL(i),right=grid.GetFacePosR(i);
            const double angular=GridMetrics::Rz::AngularReconstructionCoordinate(left,right);
            const double radial=.1*grid.GetCellCenterX(i);
            const double axial=.05*grid.GetCellCenterY(j);
            state.set(cell,{1.,radial,axial,angular,100.});
            state.X(0,cell)=.6;state.X(1,cell)=.4;
        }
        std::vector<FluidVector> delta(size),flux(size);
        std::vector<double> ds(2*size),sf(2*size);
        if(lane==2)
            TimeIntegration::evaluate_all_dimensions<FluxHLLC<PCMReconstruction>>(
                nullptr,-1,state,eos,grid,dt,delta,ds,flux,sf,nullptr,0.,1.,true,
                GridMetrics::GeometrySemantics::AxisymmetricRz);
        long double outward_j=0.,outward_mass=0.,outward_energy=0.,outward_species[2]{};
        for(int dir:{0,1}) {
            std::fill(flux.begin(),flux.end(),FluidVector{});
            std::fill(sf.begin(),sf.end(),0.);
            if(lane==2) {
                // Independent boundary integration must borrow the SAME native
                // face semantics as the actual stage delta above. An ordinary
                // point-mean sweep is a different flux, even with policy PCM.
                FluxAdmissibility::MeanThermoCache native_means;
                native_means.reset(size);
                native_means.geometry_semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
                native_means.physical_bounds={};
                FluxHLLC<PCMReconstruction>::compute_fluxes(state,eos,grid,flux,sf,dir,0.,&native_means);
            } else {
                for(int j=grid.Js();j<=grid.Je();++j)for(int i=grid.Is();i<=grid.Ie();++i) {
                    const int cell=grid.GetIndex(i,j,0);
                    double value=1.+.3*i+.7*j;
                    if(lane==0 && (dir==0?(i==grid.Is() || i==grid.Ie())
                                              :(j==grid.Js() || j==grid.Je())))value=0.;
                    flux[cell]={.2*value,.3*value,-.1*value,value,2.*value};
                    sf[cell]=.6*flux[cell].rho;sf[size+cell]=.4*flux[cell].rho;
                }
                TimeIntegration::accumulate_divergence(delta,ds,flux,sf,grid,dt,dir,2,
                    GridMetrics::GeometrySemantics::AxisymmetricRz,true);
            }
            // Independent full-ring face integrals, not the production metrics.
            for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
                const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
                for(int side:{0,1}) {
                    if(dir==0 && i!=(side?grid.Ie()-1:grid.Is()))continue;
                    if(dir==1 && j!=(side?grid.Je()-1:grid.Js()))continue;
                    const int face=grid.GetIndex(i+(dir==0?side:0),j+(dir==1?side:0),0);
                    const long double radius=side?hi:lo;
                    const long double area=dir==0?2*pi*radius*grid.dx2:pi*(hi*hi-lo*lo);
                    const long double torque=dir==0?2*pi*radius*radius*grid.dx2
                        :2*pi*(hi*hi*hi-lo*lo*lo)/3;
                    const long double sign=side?1.L:-1.L;
                    outward_j+=sign*dt*torque*flux[face].mom_w;
                    outward_mass+=sign*dt*area*flux[face].rho;
                    outward_energy+=sign*dt*area*flux[face].eng;
                    for(int s=0;s<2;++s)outward_species[s]+=sign*dt*area*sf[s*size+face];
                }
            }
        }
        long double change_j=0.,change_mass=0.,change_energy=0.,change_species[2]{},initial_abs_j=0.;
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
            const int cell=grid.GetIndex(i,j,0);
            const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
            const long double volume=pi*(hi*hi-lo*lo)*grid.dx2;
            const long double w=2*pi*(hi*hi*hi-lo*lo*lo)*grid.dx2/3;
            change_j+=w*delta[cell].mom_w;
            initial_abs_j+=w*std::abs(state.mom_w[cell]);
            change_mass+=volume*delta[cell].rho;change_energy+=volume*delta[cell].eng;
            for(int s=0;s<2;++s)change_species[s]+=volume*ds[s*size+cell];
        }
        const long double denom=initial_abs_j+std::abs(outward_j);
        const long double error=std::abs(change_j+outward_j)/denom;
        if(!(error<=1.e-12L))throw std::runtime_error("RZ torque divergence violates frozen J budget");
        const auto balance=[](long double change,long double outward,const char* name) {
            if(std::abs(change+outward)>1.e-12L*std::max(1.L,std::abs(outward)))
                throw std::runtime_error(name);
        };
        balance(change_mass,outward_mass,"RZ V mass divergence changed");
        balance(change_energy,outward_energy,"RZ V energy divergence changed");
        for(int s=0;s<2;++s)balance(change_species[s],outward_species[s],"RZ V species divergence changed");
        if(lane==2) {
            TimeIntegration::perform_stage_update(state,state,updated,delta,ds,grid,0.,1.,
                1.e-14,1.e-14,1.e6,GridMetrics::GeometrySemantics::AxisymmetricRz);
            if(updated.stage_repairs.values[0]!=0.)throw std::runtime_error("RZ torque stage repaired");
            long double final_change=0.;
            for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
                const int cell=grid.GetIndex(i,j,0);
                const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
                final_change+=2*pi*(hi*hi*hi-lo*lo*lo)*grid.dx2/3
                    *(updated.mom_w[cell]-state.mom_w[cell]);
            }
            if(std::abs(final_change+outward_j)/denom>1.e-12L)
                throw std::runtime_error("RZ actual HLLC stage J budget failed");
        }
        std::cout<<"RZ_TORQUE_BUDGET inner="<<inner<<" lane="<<lane
            <<" relative_error="<<static_cast<double>(error)
            <<" boundary_torque_impulse="<<static_cast<double>(outward_j)<<'\n';
    }
}

void test_rz_host_hydro() {
    using namespace GridMetrics;
    SpeciesManager species;
    species.add_species("gas", 1., 1., 1.4, 3.);
    IdealGas eos(1.4, species);
    const auto rz = GeometrySemantics::AxisymmetricRz;
    for (double inner : {0., 1.}) for (double omega : {0., 2.}) {
        // Physical solid-body rotation u_phi=Omega*r is regular at the axis.
        // Native m_phi is its W mean and E its V mean, rather than a point
        // state with constant u_phi. True negative-radius ghosts use the same
        // odd velocity/even energy field and independent signed integrals.
        Grid grid(amr::MAX_NG, inner, inner+1., -.5, .5, 0., 1.);
        grid.dim=2; grid.geometry="cylindrical"; grid.InitializeTopology();
        FluidState state, updated;
        const int size=grid.GetTotalSize();
        state.Preallocate(size); state.InitSpecies(1);
        updated.Preallocate(size); updated.InitSpecies(1);
        constexpr double rho=2., axial=3., pressure=5., dt=.001;
        for (int cell=0;cell<size;++cell) {
            // Padding is storage, not another physical radial cell.
            state.set(cell,{rho,0.,rho*axial,0.,pressure/.4+.5*rho*axial*axial});
            state.X(0,cell)=1.;
        }
        for (int j=0;j<grid.GetTotalY();++j) for(int i=0;i<grid.GetTotalX();++i) {
            const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
            const long double l2=l*l,h2=h*h,l3=l2*l,h3=h2*h,l4=l2*l2,h4=h2*h2;
            if (!(h>l) || (l<0.L && h>0.L))
                throw std::runtime_error("Solid-body oracle requires a one-sided physical radial cell");
            const long double sign=h<=0.L?-1.L:1.L;
            // Independent antiderivatives: V/(2*pi*dz)=int |r|dr,
            // W/(2*pi*dz)=int r^2dr, J/(2*pi*dz)=rho*Omega*int r^3dr.
            const long double v=sign*(h2-l2)/2.L,w=(h3-l3)/3.L;
            const long double angular_mean=static_cast<long double>(rho)*omega*(h4-l4)/(4.L*w);
            const long double radius_square_v=sign*(h4-l4)/(4.L*v);
            const long double energy=static_cast<long double>(pressure)/.4L
                +.5L*rho*(static_cast<long double>(axial)*axial
                    +static_cast<long double>(omega)*omega*radius_square_v);
            state.set(grid.GetIndex(i,j,0),{rho,0.,rho*axial,
                static_cast<double>(angular_mean),static_cast<double>(energy)});
        }
        const auto source_bits=[&]() {
            std::vector<std::uint64_t> words;
            for(const auto* values:std::array<const std::vector<double>*,7>{
                &state.rho,&state.mom_u,&state.mom_v,&state.mom_w,&state.eng,
                &state.enuc_rate,&state.mass_fractions})
                for(double value:*values) words.push_back(std::bit_cast<std::uint64_t>(value));
            return words;
        };
        const auto original_source=source_bits();
        std::vector<FluidVector> delta(size),flux(size);
        std::vector<double> species_delta(size),species_flux(size);
        TimeIntegration::evaluate_all_dimensions<FluxHLLC<PCMReconstruction>>(
            nullptr,-1,state,eos,grid,dt,delta,species_delta,
            flux,species_flux,nullptr,0.,1.,true,rz);
        TimeIntegration::perform_stage_update(state,state,updated,
            delta,species_delta,grid,0.,1.,1.e-14,1.e-14,1.e10,rz);
        double max_error=0.;
        for (int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i) {
            const int cell=grid.GetIndex(i,j,0);
            const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
            // Pressure flux and curvature cancel. The centrifugal V mean is
            // dt*rho*Omega^2*int r^2dr/int r dr, equivalently
            // dt*rho*Omega^2*2*(l*l+l*h+h*h)/(3*(l+h)).
            const long double radial_mean=((h*h*h-l*l*l)/3.L)/((h*h-l*l)/2.L);
            const double expected=static_cast<double>(static_cast<long double>(dt)*rho*omega*omega*radial_mean);
            close(delta[cell].mom_u,expected,"RZ Host Hydro centrifugal source");
            close(delta[cell].mom_v,0.,"RZ Host Hydro axial momentum");
            close(delta[cell].mom_w,0.,"RZ Host Hydro omega momentum");
            close(delta[cell].rho,0.,"RZ Host Hydro density");
            close(delta[cell].eng,0.,"RZ Host Hydro energy");
            close(species_delta[cell],0.,"RZ Host Hydro species");
            close(updated.mom_u[cell],expected,"RZ Host Hydro RK update");
            close(updated.mom_v[cell],rho*axial,"RZ Host Hydro updated axial momentum");
            close(updated.rho[cell],state.rho[cell],"RZ Host Hydro updated V density mean");
            close(updated.mom_w[cell],state.mom_w[cell],"RZ Host Hydro updated W angular mean");
            close(updated.eng[cell],state.eng[cell],"RZ Host Hydro updated V energy mean");
            close(updated.X(0,cell),1.,"RZ Host Hydro updated composition");
            max_error=std::max(max_error,std::abs(delta[cell].mom_u-expected));
        }
        if (updated.stage_repairs.values[0]!=0.)
            throw std::runtime_error("RZ solid-body Hydro unexpectedly repaired");
        // Reject legacy gravity before resetting output or calling its owner.
        struct UnmigratedGravity : Physical::Gravity::IGravityPolicy {
            mutable int calls=0;
            void add_sources_on_patch(std::vector<FluidVector>&,const FluidState&,
                const Grid&,double,void*) const override { ++calls; }
        } unmigrated;
        delta[0].rho=123.;
        bool rejected=false;
        try {
            TimeIntegration::evaluate_all_dimensions<FluxHLLC<PCMReconstruction>>(
                nullptr,-1,state,eos,grid,dt,delta,species_delta,
                flux,species_flux,&unmigrated,0.,1.,true,rz);
        } catch (const std::invalid_argument& error) {
            rejected=std::string(error.what()).find("not migrated")!=std::string::npos;
        }
        if (!rejected || delta[0].rho!=123. || unmigrated.calls!=0)
            throw std::runtime_error("RZ unmigrated gravity changed Hydro output");
        if(source_bits()!=original_source)
            throw std::runtime_error("RZ solid-body Hydro changed immutable source arrays");
        // Core's new single-J contract rejects a conservative RZ candidate
        // below configured bounds; the former repair fixture is not acceptance.
        FluidState low, repaired;
        low.Preallocate(size); low.InitSpecies(0);
        repaired.Preallocate(size); repaired.InitSpecies(0);
        for(int cell=0;cell<size;++cell) low.set(cell,{.5,0.,0.,0.,100.});
        std::fill(delta.begin(),delta.end(),FluidVector{});
        std::vector<double> no_species;
        bool low_rejected=false;
        try {
            TimeIntegration::perform_stage_update(low,low,repaired,delta,no_species,
                grid,0.,1.,1.,1.e-14,1.e10,rz);
        } catch(const std::runtime_error&) {low_rejected=true;}
        if(!low_rejected || repaired.stage_repairs.values[0]!=0.)
            throw std::runtime_error("RZ Hydro repaired a forbidden conservative candidate");
        std::cout<<"RZ_HOST_HYDRO inner="<<inner<<" omega="<<omega
            <<" max_radial_error="<<max_error<<" repair_volume="
            <<repaired.stage_repairs.values[1]<<'\n';
    }
}


void test_rz_host_cfl() {
    using namespace GridMetrics;
    SpeciesManager species;
    species.add_species("gas",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    constexpr double cfl=.4,rho=2.,pressure=5.;
    const double sound=std::sqrt(1.4*pressure/rho);
    for (double inner : {0.,1.}) {
        Grid grid(amr::MAX_NG,inner,inner+1.,-1.,1.,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology();
        FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(1);
        double zero_swirl_dt=0.;
        for (double swirl : {0.,2.}) {
            double reference=std::numeric_limits<double>::max();
            for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
                const int cell=grid.GetIndex(i,j,0);
                const double radius=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
                const double radial=.1+.2*radius,axial=-.5+.3*z;
                // Native m_phi is the W mean of rho*Omega*r and E is a V
                // mean. Integrate those two polynomials independently; a
                // point-state swirl momentum is not a native annular mean.
                const double left=grid.x1_min+(i-grid.Is())*grid.dx1;
                const double right=left+grid.dx1;
                const double angular_radius=.75*(std::pow(right,4)-std::pow(left,4))
                    /(std::pow(right,3)-std::pow(left,3));
                const double radial_square_mean=.5*(right*right+left*left);
                state.set(cell,{rho,rho*radial,rho*axial,rho*swirl*angular_radius,
                    pressure/.4+.5*rho*(radial*radial+axial*axial
                        +swirl*swirl*radial_square_mean)});
                state.X(0,cell)=1.;
                if(i>=grid.Is() && i<grid.Ie() && j>=grid.Js() && j<grid.Je()) {
                    // Independent two-face acoustic transport bound, dr != dz.
                    const double rate=(std::abs(radial)+sound)*amr::BLOCK_NX
                        +(std::abs(axial)+sound)*amr::BLOCK_NY/2.;
                    reference=std::min(reference,.5*cfl/rate);
                }
            }
            const double serial=adaptive_dt(state,eos,grid,cfl,false,
                GeometrySemantics::AxisymmetricRz);
            const double parallel=adaptive_dt(state,eos,grid,cfl,true,
                GeometrySemantics::AxisymmetricRz);
            close(serial,reference,"RZ Host CFL r/z physical transport bound");
            close(parallel,reference,"RZ Host parallel CFL bound");
            if(serial!=parallel)throw std::runtime_error("RZ CFL reduction schedule drift");
            if(swirl==0.)zero_swirl_dt=serial;
            else close(serial,zero_swirl_dt,"inactive phi entered RZ acoustic CFL");
            std::cout<<"RZ_HOST_CFL inner="<<inner<<" swirl="<<swirl
                <<" dt="<<serial<<" reference="<<reference
                <<" absolute_error="<<std::abs(serial-reference)<<'\n';
        }
        state.rho[grid.GetIndex(grid.Is(),grid.Js(),0)]=
            std::numeric_limits<double>::quiet_NaN();
        bool rejected=false;
        try { (void)adaptive_dt(state,eos,grid,cfl,false,GeometrySemantics::AxisymmetricRz); }
        catch(const std::runtime_error&) { rejected=true; }
        if(!rejected)throw std::runtime_error("RZ CFL accepted invalid active density");
    }
}


void test_rz_mixed_hydro_stage(int direction,double inner) {
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    SpeciesManager species;
    species.add_species("gas0",1.,1.,1.4,3.);
    species.add_species("gas1",2.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> policy(eos,rz);
    const Numerics::IHydroSolver& hydro=policy;
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> legacy(eos);
    if(hydro.geometry_semantics()!=rz
        || legacy.geometry_semantics()!=GridMetrics::GeometrySemantics::Existing)
        throw std::runtime_error("Host Hydro type-erased chart identity");
    bool invalid_rejected=false;
    try {
        Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> invalid(
            eos,static_cast<GridMetrics::GeometrySemantics>(255));
    } catch(const std::invalid_argument&) { invalid_rejected=true; }
    if(!invalid_rejected)throw std::runtime_error("Unknown Hydro chart accepted");
    NumericsConfig numerics{};
    numerics.entropy_fix_coeff=0.;numerics.hll_roe_wave_speed=true;
    numerics.sml_rho=1.e-14;numerics.min_eint=1.e-14;numerics.max_eint=1.e10;
    SimConfig config{};
    config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=direction==0?2:1;
    config.grid.nblockx2=direction==0?1:2;config.grid.nblockx3=0;
    config.grid.x1_min=inner;config.grid.x1_max=inner+2.;
    config.grid.x2_min=-1.;config.grid.x2_max=1.;
    config.grid.amr_max_blocks=32;config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    amr::AMRControl control(32,2);
    control.tree->LoadLeafGrid(config,0,{1,1,1,1,0},
        {0,1,0,1,static_cast<std::uint32_t>(direction==0?1:0)},
        {0,0,1,1,static_cast<std::uint32_t>(direction==0?0:1)},{0,0,0,0,0});
    const auto& active=control.tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles;
    const FluidVector reference{2.,0.,6.,0.,21.5}; // p5, vz3, zero swirl.
    for(std::size_t n=0;n<active.size();++n) {
        handles.push_back({{4000+n},{97}});
        auto& block=control.pool->GetBlock(active[n]);
        block.fluid_state.InitSpecies(2);block.state_next.InitSpecies(2);
        for(int cell=0;cell<block.grid.GetTotalSize();++cell) {
            block.fluid_state.set(cell,reference);
            block.fluid_state.X(0,cell)=.6;block.fluid_state.X(1,cell)=.4;
        }
    }
    control.BindActiveHandles(handles);control.flux_register.EnsureSpecies(2);
    control.flux_register.Clear();
    BCHandler boundary(config,rz);boundary.bind(eos,species);
    for(int id:active) {
        auto& block=control.pool->GetBlock(id);
        boundary.apply(block.fluid_state,block.grid);
    }
    control.ghost_exchange.ExecuteExchange(control.pool,control.tree,2,
        &amr::Block::fluid_state,handles,amr::CoordinateSeamGeometry::RzAxisymmetric);
    constexpr double dt=.001;
    double max_delta=0.;
    for(int id:active) {
        auto& block=control.pool->GetBlock(id);
        const auto& g=block.grid;const int size=g.GetTotalSize();
        std::vector<FluidVector> delta(size);
        std::vector<double> ds(2*size);
        hydro.evaluate_patch(&control,id,block.fluid_state,g,dt,delta,ds,
            nullptr,numerics,1.);
        hydro.update_patch(block.fluid_state,block.fluid_state,
            block.state_next,delta,ds,g,0.,1.,numerics);
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const auto d=delta[g.GetIndex(i,j,0)];
            for(double v:{d.rho,d.mom_u,d.mom_v,d.mom_w,d.eng}) {
                close(v,0.,"RZ mixed Hydro uniform-state derivative");
                max_delta=std::max(max_delta,std::abs(v));
            }
        }
        if(block.state_next.stage_repairs.values[0]!=0.)
            throw std::runtime_error("RZ mixed Hydro manufactured repair");
    }
    const auto& topology=control.RequireFluxTopologyPlan(2,rz,-1,true);
    if(topology.semantics!=rz)throw std::runtime_error("Hydro registered legacy AMR chart");
    double max_register=0.;
    for(int id:active)for(int face=0;face<4;++face) {
        if(!control.flux_register.HasData(id,face))continue;
        const int count=face/2==0?amr::BLOCK_NY:amr::BLOCK_NX;
        for(int cell=0;cell<count;++cell) {
            const auto f=control.flux_register.GetSummedFlux(id,face,cell);
            for(double v:{f.rho,f.mom_u,f.mom_v,f.mom_w,f.eng}) {
                close(v,0.,"RZ mixed Hydro constant face-register balance");
                max_register=std::max(max_register,std::abs(v));
            }
        }
    }
    for(int id:active) {
        auto& block=control.pool->GetBlock(id);
        boundary.apply(block.state_next,block.grid);
    }
    control.ghost_exchange.ExecuteExchange(control.pool,control.tree,2,
        &amr::Block::state_next,handles,amr::CoordinateSeamGeometry::RzAxisymmetric);
    control.ApplyReflux(dt,&amr::Block::state_next,rz,true);
    double max_state_error=0.;
    for(int id:active) {
        const auto& block=control.pool->GetBlock(id);const auto& g=block.grid;
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const int cell=g.GetIndex(i,j,0);const auto u=block.state_next.get(cell);
            const double errors[]{u.rho-reference.rho,u.mom_u-reference.mom_u,
                u.mom_v-reference.mom_v,u.mom_w-reference.mom_w,u.eng-reference.eng,
                block.state_next.X(0,cell)-.6,block.state_next.X(1,cell)-.4};
            for(double v:errors) {
                close(v,0.,"RZ mixed Hydro stage/reflux state drift");
                max_state_error=std::max(max_state_error,std::abs(v));
            }
        }
    }
    std::cout<<"RZ_MIXED_HYDRO direction="<<direction<<" inner="<<inner
        <<" max_delta="<<max_delta<<" max_register="<<max_register
        <<" max_state_error="<<max_state_error<<'\n';
}

// Test observer delegates every update to the actual hydro owner. It only
// recomputes physical boundary face fluxes from the exact stage input, using
// the same EOS/HLLC policy and independent full-ring surface integration.
class RzBoundaryBudgetObserver final : public Numerics::IHydroSolver {
public:
    RzBoundaryBudgetObserver(const Numerics::IHydroSolver& owner,const IdealGas& eos,
        const Physical::Gravity::ExternalGravity* external=nullptr)
        :external_(external),owner_(owner),eos_(eos){}
    GridMetrics::GeometrySemantics geometry_semantics() const noexcept override {
        return owner_.geometry_semantics();
    }
    void evaluate_patch(amr::AMRControl* control,int block_id,
        const FluidState& state,const Grid& grid,double dt,
        std::vector<FluidVector>& dU,std::vector<double>& ds,
        const Physical::Gravity::IGravityPolicy* gravity,
        const NumericsConfig& cfg,double stage_weight=1.,
        void* stream=nullptr,
        const arch::boundary::HostHydroBoundaryAuthority* boundary=nullptr) const override
    {
        std::array<long double,5> local{};
        long double unweighted_torque=0.,patch_torque=0.,same_torque=0.,mixed_torque=0.;
        FluxAdmissibility::MeanThermoCache means;
        means.reset(grid.GetTotalSize());means.roe_wave_speed=cfg.hll_roe_wave_speed;
        means.geometry_semantics=owner_.geometry_semantics();
        means.physical_bounds={cfg.sml_rho,cfg.min_eint,cfg.max_eint};
        if(boundary)means.hydro_boundary=boundary->require_view(control,block_id,state,grid);
        std::vector<FluidVector> flux(grid.GetTotalSize());
        std::vector<double> species_flux(state.GetNumSpecies()*grid.GetTotalSize());
        const auto& block=control->pool->GetBlock(block_id);
        const long double pi=std::acos(-1.L);
        for(int dir=0;dir<2;++dir) {
            std::fill(flux.begin(),flux.end(),FluidVector{});
            std::fill(species_flux.begin(),species_flux.end(),0.);
            FluxHLLC<PCMReconstruction>::compute_fluxes(state,eos_,grid,flux,
                species_flux,dir,cfg.entropy_fix_coeff,&means);
            for(int side=0;side<2;++side) {
                const bool external_face=block.face_neighbors[2*dir+side].count==0;
                const int count=dir==0?grid.Je()-grid.Js():grid.Ie()-grid.Is();
                for(int n=0;n<count;++n) {
                    const int i=dir==0?(side?grid.Ie():grid.Is()):grid.Is()+n;
                    const int j=dir==0?grid.Js()+n:(side?grid.Je():grid.Js());
                    const int c=grid.GetIndex(i,j,0);
                    const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
                    const long double A=dir==0?2.L*pi*l*grid.dx2:pi*(h*h-l*l);
                    const long double T=dir==0?2.L*pi*l*l*grid.dx2
                        :2.L*pi*(h*h*h-l*l*l)/3.L;
                    const long double factor=(side?1.L:-1.L)*dt*stage_weight;
                    patch_torque+=factor*T*flux[c].mom_w;
                    if(!external_face) {
                        if(block.face_neighbors[2*dir+side].level_diff==0)same_torque+=factor*T*flux[c].mom_w;
                        else mixed_torque+=factor*T*flux[c].mom_w;
                        continue;
                    }
                    local[0]+=factor*A*flux[c].rho;
                    local[1]+=factor*A*flux[c].eng;
                    local[2]+=factor*T*flux[c].mom_w;
                    unweighted_torque+=(side?1.L:-1.L)*dt*T*flux[c].mom_w;
                    for(int k=0;k<2;++k)
                        local[3+k]+=factor*A*species_flux[k*grid.GetTotalSize()+c];
                }
            }
        }
        // Read the actual stage state; the source update remains in the
        // existing ExternalGravity/TimeIntegratorHelper owners. Independent
        // endpoint integrals distinguish V energy work from W angular impulse.
        std::array<long double,5> applied{};
        long double unweighted_applied=0.;
        if(external_) {
            if(gravity!=external_ || grid.GetFacePosL(grid.Is())<=0.)
                throw std::runtime_error("off-axis external torque identity");
            const long double pi=std::acos(-1.L);
            for(int j=grid.Js();j<grid.Je();++j)
                for(int i=grid.Is();i<grid.Ie();++i) {
                    const int c=grid.GetIndex(i,j,0);
                    const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
                    const long double V=pi*(h*h-l*l)*grid.dx2;
                    const long double W=2.L*pi*(h*h*h-l*l*l)*grid.dx2/3.L;
                    const long double angular=dt*state.rho[c]*external_->g_z*W;
                    applied[2]+=stage_weight*angular;
                    unweighted_applied+=angular;
                    applied[1]+=stage_weight*dt*state.mom_w[c]*external_->g_z*V;
                }
        }
        owner_.evaluate_patch(control,block_id,state,grid,dt,dU,ds,
            gravity,cfg,stage_weight,stream,boundary);
        long double patch_delta=0.;
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
            const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
            const long double W=2.L*pi*(h*h*h-l*l*l)*grid.dx2/3.L;
            patch_delta+=stage_weight*W*dU[grid.GetIndex(i,j,0)].mom_w;
        }
        std::lock_guard lock(mutex_);
        patch_torque_+=patch_torque;patch_delta_+=patch_delta;
        same_torque_+=same_torque;mixed_torque_+=mixed_torque;
        for(int k=0;k<5;++k) {outward_[k]+=local[k];applied_[k]+=applied[k];}
        unweighted_applied_+=unweighted_applied;
        unweighted_torque_+=unweighted_torque;
        ++stage_calls_;
    }
    void update_patch(const FluidState& old,const FluidState& current,FluidState& next,
        const std::vector<FluidVector>& dU,const std::vector<double>& ds,
        const Grid& grid,double old_weight,double flux_weight,
        const NumericsConfig& cfg,void* stream=nullptr) const override {
        owner_.update_patch(old,current,next,dU,ds,grid,old_weight,flux_weight,cfg,stream);
    }
    std::array<long double,5> outward() const {
        std::lock_guard lock(mutex_);return outward_;
    }
    long double unweighted_torque() const {
        std::lock_guard lock(mutex_);return unweighted_torque_;
    }
    std::array<long double,5> applied() const {
        std::lock_guard lock(mutex_);return applied_;
    }
    long double unweighted_applied() const {
        std::lock_guard lock(mutex_);return unweighted_applied_;
    }
    std::array<long double,4> patch_budget() const {
        std::lock_guard lock(mutex_);return {patch_torque_,patch_delta_,same_torque_,mixed_torque_};
    }
    int stage_calls() const {std::lock_guard lock(mutex_);return stage_calls_;}
private:
    const Physical::Gravity::ExternalGravity* external_=nullptr;
    mutable std::array<long double,5> applied_{};
    mutable long double unweighted_applied_=0.;
    const Numerics::IHydroSolver& owner_;
    const IdealGas& eos_;
    mutable std::mutex mutex_;
    mutable std::array<long double,5> outward_{};
    mutable int stage_calls_=0;
    mutable long double unweighted_torque_=0.,patch_torque_=0.,patch_delta_=0.,same_torque_=0.,mixed_torque_=0.;
};

template<typename Solver>
void test_rz_rotating_boundary_budget(int direction,double inner,bool open=false,double external_phi=0.) {
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    SpeciesManager species;
    species.add_species("gas0",1.,1.,1.4,3.);
    species.add_species("gas1",2.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> policy(eos,rz);
    if(external_phi!=0. && inner<=0.)throw std::invalid_argument("body torque audit is off-axis");
    Physical::Gravity::ExternalGravity external(0.,0.,external_phi);
    const auto* gravity=external_phi==0.?nullptr:&external;
    RzBoundaryBudgetObserver observer(policy,eos,gravity);
    const Numerics::IHydroSolver& hydro=observer;
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> legacy(eos);
    if(hydro.geometry_semantics()!=rz
        || legacy.geometry_semantics()!=GridMetrics::GeometrySemantics::Existing)
        throw std::runtime_error("Host Hydro type-erased chart identity");
    bool invalid_rejected=false;
    try {
        Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> invalid(
            eos,static_cast<GridMetrics::GeometrySemantics>(255));
    } catch(const std::invalid_argument&) { invalid_rejected=true; }
    if(!invalid_rejected)throw std::runtime_error("Unknown Hydro chart accepted");
    NumericsConfig numerics{};
    numerics.entropy_fix_coeff=0.;numerics.hll_roe_wave_speed=true;
    numerics.sml_rho=1.e-14;numerics.min_eint=1.e-14;numerics.max_eint=1.e10;
    SimConfig config{};
    config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=direction==0?2:1;
    config.grid.nblockx2=direction==0?1:2;config.grid.nblockx3=0;
    config.grid.x1_min=inner;config.grid.x1_max=inner+2.;
    config.grid.x2_min=-1.;config.grid.x2_max=1.;
    config.grid.amr_max_blocks=32;config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="reflecting";
    config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="reflecting";
    if(open) {
        config.grid.x1r_boundary_type="outflow";
        config.grid.x1l_boundary_type=inner==0.?"reflecting":"outflow";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="outflow";
    }
    amr::AMRControl control(32,2);
    control.tree->LoadLeafGrid(config,0,{1,1,1,1,0},
        {0,1,0,1,static_cast<std::uint32_t>(direction==0?1:0)},
        {0,0,1,1,static_cast<std::uint32_t>(direction==0?0:1)},{0,0,0,0,0});
    const auto& active=control.tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles;
    for(std::size_t n=0;n<active.size();++n) {
        handles.push_back({{4000+n},{97}});
        auto& block=control.pool->GetBlock(active[n]);
        block.fluid_state.InitSpecies(2);block.state_next.InitSpecies(2);block.state_scratch.InitSpecies(2);
        for(int cell=0;cell<block.grid.GetTotalSize();++cell) {
            const auto& g=block.grid;
            const int i=cell%g.stride_y,j=(cell/g.stride_y)%g.GetTotalY();
            const double left=g.GetFacePosL(i),right=g.GetFacePosR(i);
            const double radius=g.GetCellCenterX(i),z=g.GetCellCenterY(j);
            const double pi=std::acos(-1.);
            const double rho=2.+.1*std::cos(pi*(radius-inner))*.1*std::cos(pi*z);
            const double vr=.03*std::sin(pi*(radius-inner)/2.);
            const double vz=.02*std::sin(pi*(z+1.)/2.);
            // Odd regular u_phi at the axis. Ghost values are filled by the
            // actual boundary/AMR owners before the first stage.
            const double wc=right<=0.?
                -GridMetrics::Rz::AngularReconstructionRadius(-right,-left)
                :GridMetrics::Rz::AngularReconstructionRadius(left,right);
            const double vp=.15*wc*(1.+.2*std::cos(pi*z));
            const double E=12.5+.5*rho*(vr*vr+vz*vz+vp*vp);
            block.fluid_state.set(cell,{rho,rho*vr,rho*vz,rho*vp,E});
            const double X=.6+.02*std::cos(pi*z);
            block.fluid_state.X(0,cell)=X;block.fluid_state.X(1,cell)=1.-X;
        }
    }
    control.BindActiveHandles(handles);control.flux_register.EnsureSpecies(2);
    control.flux_register.Clear();
    BCHandler boundary(config,rz);boundary.bind(eos,species);
    for(int id:active) {
        auto& block=control.pool->GetBlock(id);
        boundary.apply(block.fluid_state,block.grid);
    }
    control.ghost_exchange.ExecuteExchange(control.pool,control.tree,2,
        &amr::Block::fluid_state,handles,amr::CoordinateSeamGeometry::RzAxisymmetric);

    using namespace arch::state;
    using namespace arch::scheduler;
    StateResidencyLedger ledger({97});
    for(auto handle:handles) {
        ledger.register_block(handle,{1},{1,CompletionState::Complete});
        ledger.publish_ghost({handle,StateSlot::Current},ExecutionSide::Host,
            {1},{2,CompletionState::Complete});
    }
    MonotonicSchedulerClock clock(2,1);
    StageExecutionContext context{ExecutionSide::Host,ledger,clock};
    int completed_gates=0;
    bind_rz_geometry_fixture_acceptance(context,control,handles,eos,numerics,completed_gates);
    ScopedStageBinding scope(context,handles);
    // Independent full-ring integral budget, not production metric helpers.
    const auto totals=[&]() {
        std::array<long double,6> sum{};
        const long double pi=std::acos(-1.L);
        for(int id:active) {
            const auto& block=control.pool->GetBlock(id);const auto& g=block.grid;
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                const int c=g.GetIndex(i,j,0);
                const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
                const long double V=pi*(h*h-l*l)*g.dx2;
                const long double W=2.L*pi*(h*h*h-l*l*l)*g.dx2/3.L;
                const auto& u=block.fluid_state;
                sum[0]+=u.rho[c]*V;sum[1]+=u.eng[c]*V;
                sum[2]+=u.mom_w[c]*W;sum[3]+=std::abs(u.mom_w[c])*W;
                sum[4]+=u.rho[c]*u.X(0,c)*V;sum[5]+=u.rho[c]*u.X(1,c)*V;
            }
        }
        return sum;
    };
    const auto before=totals();
    long double maxJ=0.,maxM=0.,maxE=0.,maxSpecies=0.;
    double maxTorqueRegister=0.;
    constexpr int steps=10;
    long double cumulative_reflux=0.;
    for(int step=0;step<steps;++step) {
        complete_boundary(context,handles,StateSlot::Current,
            StateVersion{clock.last_version()},
            [&](StateSlot,StateVersion,CompletionToken token) {
                for(int id:active) {
                    auto& block=control.pool->GetBlock(id);
                    boundary.apply(block.fluid_state,block.grid);
                }
                control.ghost_exchange.ExecuteExchange(control.pool,control.tree,2,
                    &amr::Block::fluid_state,handles,
                    amr::CoordinateSeamGeometry::RzAxisymmetric,
                    {numerics.sml_rho,numerics.min_eint,numerics.max_eint});
                return token;
            });
        const auto snapshot=[&]() {
            std::vector<std::uint64_t> words;
            for(int id:active) {
                const auto& b=control.pool->GetBlock(id);
                for(int c=0;c<b.grid.GetTotalSize();++c) {
                    const auto u=b.fluid_state.get(c);
                    for(double value:{u.rho,u.mom_u,u.mom_v,u.mom_w,u.eng,
                        b.fluid_state.X(0,c),b.fluid_state.X(1,c)})
                        words.push_back(std::bit_cast<std::uint64_t>(value));
                }
            }
            return words;
        };
        const auto preflight_state=gravity?snapshot():std::vector<std::uint64_t>{};
        try {
            Solver::solve(control,1.e-4,boundary,gravity,&hydro,numerics);
        } catch(const std::invalid_argument& error) {
            if(gravity && std::string(error.what())=="RZ gravity requires authoritative finite-ring contract") {
                if(observer.stage_calls()!=0 || snapshot()!=preflight_state)
                    throw std::runtime_error("external torque rejection partially updated current state");
                std::cerr<<"RZ_APPLIED_TORQUE_PREFLIGHT stage_calls=0 state_words="
                    <<preflight_state.size()<<" state_bits_unchanged=1 reason="<<error.what()<<'\n';
            }
            throw;
        }
        for(int id:active) {
            const auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
            const long double pi=std::acos(-1.L);
            for(int face=0;face<4;++face) {
                if(b.face_neighbors[face].level_diff!=1||!control.flux_register.HasData(id,face))continue;
                const int dir=face/2,side=face%2,count=dir==0?g.Je()-g.Js():g.Ie()-g.Is();
                for(int n=0;n<count;++n) {
                    const int i=dir==0?(side?g.Ie():g.Is()):g.Is()+n;
                    const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
                    const long double A=dir==0?2.L*pi*l*g.dx2:pi*(h*h-l*l);
                    cumulative_reflux+=(side?-1.L:1.L)*1.e-4L*A*
                        control.flux_register.GetSummedFlux(id,face,n).mom_w;
                }
            }
        }
        const auto now=totals();
        const auto out=observer.outward();
        const auto applied=observer.applied();
        if(!open) {
            // A closed stationary wall has no mass, total-energy, angular or
            // species transport. Accounting for a nonzero measured outflow
            // cannot establish this stronger physical boundary condition.
            const long double scale[5]{before[0],before[1],before[3],before[4],before[5]};
            for(int field=0;field<5;++field)
                if(std::abs(out[field])>1.e-12L*scale[field])
                    throw std::runtime_error("RZ reflected physical wall transported a conserved quantity");
        }
        const long double jerror=std::abs(now[2]-before[2]+out[2]-applied[2])
            /(before[3]+std::abs(out[2])+std::abs(applied[2]));
        const long double merror=std::abs(now[0]-before[0]+out[0])
            /(before[0]+std::abs(out[0]));
        const long double eerror=std::abs(now[1]-before[1]+out[1]-applied[1])
            /(before[1]+std::abs(out[1])+std::abs(applied[1]));
        const long double xerror=std::max(std::abs(now[4]-before[4]+out[3])
            /(before[4]+std::abs(out[3])),std::abs(now[5]-before[5]+out[4])
            /(before[5]+std::abs(out[4])));
        if(jerror>1.e-12L||merror>1.e-12L||eerror>1.e-12L||xerror>1.e-12L) {
            std::cerr<<"RZ_MIXED_BUDGET_FAILURE method="<<Solver::name()<<" direction="<<direction
                <<" inner="<<inner<<" open="<<open<<" step="<<step<<" J="<<double(jerror)
                <<" mass="<<double(merror)<<" E="<<double(eerror)<<" species="<<double(xerror)
                <<" dJ="<<double(now[2]-before[2])<<" out="<<double(out[2])
                <<" all_faces="<<double(observer.patch_budget()[0])
                <<" dU_W="<<double(observer.patch_budget()[1])
                <<" reflux="<<double(cumulative_reflux)
                <<" patch_telescope="<<double(observer.patch_budget()[0]+observer.patch_budget()[1])
                <<" stage_update="<<double(now[2]-before[2]-observer.patch_budget()[1]-cumulative_reflux)
                <<" same_level="<<double(observer.patch_budget()[2])
                <<" coarse_fine_minus_reflux="<<double(observer.patch_budget()[3]-cumulative_reflux)
                <<" join="<<double(observer.patch_budget()[0]-out[2]-cumulative_reflux)<<'\n';
            throw std::runtime_error("RZ rotating mixed-AMR closed science budget");
        }
        maxJ=std::max(maxJ,jerror);maxM=std::max(maxM,merror);
        maxE=std::max(maxE,eerror);maxSpecies=std::max(maxSpecies,xerror);
        for(int id:active) {
            const auto& block=control.pool->GetBlock(id);
            for(double repair:block.fluid_state.stage_repairs.values)
                if(repair!=0.)throw std::runtime_error("RZ rotating budget used repair");
            for(int face=0;face<4;++face)if(control.flux_register.HasData(id,face)) {
                const int count=face/2==0?amr::BLOCK_NY:amr::BLOCK_NX;
                for(int c=0;c<count;++c)maxTorqueRegister=std::max(maxTorqueRegister,
                    std::abs(control.flux_register.GetSummedFlux(id,face,c).mom_w));
            }
        }
    }
    if(maxTorqueRegister==0.)throw std::runtime_error("rotating fixture never exercised torque reflux");
    const auto& topology=control.RequireFluxTopologyPlan(2,rz,-1,true);
    if(!topology.angular_transport)throw std::runtime_error("rotating hydro lost torque identity");
    const auto final_out=observer.outward();
    const auto final_applied=observer.applied();
    if(open && final_out[2]==0.)throw std::runtime_error("open fixture has zero external torque");
    const int stages=std::is_same_v<Solver,SolverEuler>?1:(std::is_same_v<Solver,SolverRK2>?2:3);
    if(completed_gates!=steps*(stages+2))
        throw std::runtime_error("rotating geometry fixture missed actual initial/stage/Current EOS acceptance");
    if(observer.stage_calls()!=steps*stages*static_cast<int>(active.size()))
        throw std::runtime_error("boundary budget missed a real RK patch-stage");
    const auto final_state=totals();
    const long double naive_error=std::abs(final_state[2]-before[2]+observer.unweighted_torque()-final_applied[2])
        /(before[3]+std::abs(observer.unweighted_torque()));
    if(open && stages>1 && naive_error<=1.e-12L)
        throw std::runtime_error("wrong RK boundary stage accounting escaped negative control");
    const long double missing_applied_error=std::abs(final_state[2]-before[2]+final_out[2])
        /(before[3]+std::abs(final_out[2]));
    const long double wrong_applied_stage_error=std::abs(final_state[2]-before[2]
        +final_out[2]-observer.unweighted_applied())
        /(before[3]+std::abs(final_out[2])+std::abs(observer.unweighted_applied()));
    if(gravity && (final_applied[2]==0. || final_applied[1]==0. || missing_applied_error<=1.e-12L))
        throw std::runtime_error("missing applied torque negative control escaped");
    if(gravity && stages>1 && wrong_applied_stage_error<=1.e-12L)
        throw std::runtime_error("wrong applied RK stage accounting escaped");
    std::cout<<"RZ_ROTATING_BUDGET external_phi="<<external_phi<<" open="<<open<<" method="<<Solver::name()<<" direction="<<direction
        <<" inner="<<inner<<" steps="<<steps<<" J_error="<<static_cast<double>(maxJ)
        <<" mass_error="<<static_cast<double>(maxM)<<" E_error="<<static_cast<double>(maxE)
        <<" species_error="<<static_cast<double>(maxSpecies)
        <<" applied_torque="<<static_cast<double>(final_applied[2])
        <<" applied_energy="<<static_cast<double>(final_applied[1])
        <<" missing_applied_error="<<static_cast<double>(missing_applied_error)
        <<" wrong_applied_stage_error="<<static_cast<double>(wrong_applied_stage_error)
        <<" outward_torque="<<static_cast<double>(final_out[2])
        <<" naive_stage_error="<<static_cast<double>(naive_error)
        <<" stage_calls="<<observer.stage_calls()<<" max_torque_register="<<maxTorqueRegister<<'\n';
}

template<typename Solver>
void test_rz_scheduled_hydro(int direction,double inner) {
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    SpeciesManager species;
    species.add_species("gas0",1.,1.,1.4,3.);
    species.add_species("gas1",2.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> policy(eos,rz);
    const Numerics::IHydroSolver& hydro=policy;
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> legacy(eos);
    if(hydro.geometry_semantics()!=rz
        || legacy.geometry_semantics()!=GridMetrics::GeometrySemantics::Existing)
        throw std::runtime_error("Host Hydro type-erased chart identity");
    bool invalid_rejected=false;
    try {
        Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> invalid(
            eos,static_cast<GridMetrics::GeometrySemantics>(255));
    } catch(const std::invalid_argument&) { invalid_rejected=true; }
    if(!invalid_rejected)throw std::runtime_error("Unknown Hydro chart accepted");
    NumericsConfig numerics{};
    numerics.entropy_fix_coeff=0.;numerics.hll_roe_wave_speed=true;
    numerics.sml_rho=1.e-14;numerics.min_eint=1.e-14;numerics.max_eint=1.e10;
    SimConfig config{};
    config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=direction==0?2:1;
    config.grid.nblockx2=direction==0?1:2;config.grid.nblockx3=0;
    config.grid.x1_min=inner;config.grid.x1_max=inner+2.;
    config.grid.x2_min=-1.;config.grid.x2_max=1.;
    config.grid.amr_max_blocks=32;config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    amr::AMRControl control(32,2);
    control.tree->LoadLeafGrid(config,0,{1,1,1,1,0},
        {0,1,0,1,static_cast<std::uint32_t>(direction==0?1:0)},
        {0,0,1,1,static_cast<std::uint32_t>(direction==0?0:1)},{0,0,0,0,0});
    const auto& active=control.tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles;
    const FluidVector reference{2.,0.,6.,0.,21.5}; // p5, vz3, zero swirl.
    for(std::size_t n=0;n<active.size();++n) {
        handles.push_back({{4000+n},{97}});
        auto& block=control.pool->GetBlock(active[n]);
        block.fluid_state.InitSpecies(2);block.state_next.InitSpecies(2);block.state_scratch.InitSpecies(2);
        for(int cell=0;cell<block.grid.GetTotalSize();++cell) {
            block.fluid_state.set(cell,reference);
            block.fluid_state.X(0,cell)=.6;block.fluid_state.X(1,cell)=.4;
        }
    }
    control.BindActiveHandles(handles);control.flux_register.EnsureSpecies(2);
    control.flux_register.Clear();
    BCHandler boundary(config,rz);boundary.bind(eos,species);
    for(int id:active) {
        auto& block=control.pool->GetBlock(id);
        boundary.apply(block.fluid_state,block.grid);
    }
    control.ghost_exchange.ExecuteExchange(control.pool,control.tree,2,
        &amr::Block::fluid_state,handles,amr::CoordinateSeamGeometry::RzAxisymmetric);

    using namespace arch::state;
    using namespace arch::scheduler;
    StateResidencyLedger ledger({97});
    for(auto handle:handles) {
        ledger.register_block(handle,{1},{1,CompletionState::Complete});
        ledger.publish_ghost({handle,StateSlot::Current},ExecutionSide::Host,
            {1},{2,CompletionState::Complete});
    }
    MonotonicSchedulerClock clock(2,1);
    StageExecutionContext context{ExecutionSide::Host,ledger,clock};
    int completed_gates=0;
    bind_rz_geometry_fixture_acceptance(context,control,handles,eos,numerics,completed_gates);
    ScopedStageBinding scope(context,handles);
    // Mismatch must fail before advancing the clock/ledger. Use the actual
    // scheduled entry, not only its helper.
    BCHandler wrong_boundary(config);
    control.flux_register.AddFineFlux(active.front(),0,0,{123.,0.,0.,0.,0.},1.);
    bool mismatch=false;
    try { Solver::solve(control,.001,wrong_boundary,nullptr,&hydro,numerics); }
    catch(const std::invalid_argument&) { mismatch=true; }
    if(!mismatch || clock.last_version()!=1 || clock.last_token()!=2)
        throw std::runtime_error("RZ scheduler mismatch mutated publication");
    for(auto h:handles)
        if(ledger.inspect({h,StateSlot::Current}).interior.version!=StateVersion{1})
            throw std::runtime_error("RZ mismatch changed state ledger");
    if(!control.flux_register.HasData(active.front(),0)
        ||control.flux_register.GetSummedFlux(active.front(),0,0).rho!=123.)
        throw std::runtime_error("RZ mismatch cleared accumulated flux");
    Solver::solve(control,.001,boundary,nullptr,&hydro,numerics);
    const std::uint64_t stages=std::is_same_v<Solver,SolverEuler>?1:
        (std::is_same_v<Solver,SolverRK2>?2:3);
    if(clock.last_version()!=1+stages+1)
        throw std::runtime_error("RZ scheduler publication count");
    if(completed_gates!=static_cast<int>(stages+1))
        throw std::runtime_error("scheduled geometry fixture missed actual stage/Current EOS acceptance");
    double error=0.;
    for(std::size_t n=0;n<active.size();++n) {
        const auto& block=control.pool->GetBlock(active[n]);
        const auto& g=block.grid;
        const auto state=ledger.inspect({handles[n],StateSlot::Current});
        // Final reflux now completes real Current BC/exchange and the actual
        // EOS gate before readiness. Require matching Host ghosts, replacing
        // the former expected invalid ghosts from the path without a gate.
        if(state.interior.version.value!=clock.last_version()
            ||state.interior.residency!=StateResidency::HostValid
            ||state.ghost.residency!=StateResidency::HostValid
            ||state.ghost.version!=state.interior.version
            ||state.ghost_source_version!=state.interior.version
            ||!is_complete(state.ghost.completion)
            ||state.ghost.pending_transfer!=PendingTransferPhase::None)
            throw std::runtime_error("RZ final reflux ledger identity");
        ledger.require_readable({handles[n],StateSlot::Current},
            {ExecutionSide::Host,StateVersion{clock.last_version()},true,true});
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const int cell=g.GetIndex(i,j,0);
            const auto u=block.fluid_state.get(cell);
            for(double v:{u.rho-reference.rho,u.mom_u-reference.mom_u,
                u.mom_v-reference.mom_v,u.mom_w-reference.mom_w,u.eng-reference.eng,
                block.fluid_state.X(0,cell)-.6,block.fluid_state.X(1,cell)-.4}) {
                close(v,0.,"RZ scheduled mixed-AMR constant-state drift");
                error=std::max(error,std::abs(v));
            }
        }
    }
    if(control.RequireFluxTopologyPlan(2,rz).semantics!=rz)
        throw std::runtime_error("Scheduled Hydro reflux chart mismatch");
    std::cout<<"RZ_SCHEDULED_HYDRO method="<<Solver::name()
        <<" direction="<<direction<<" inner="<<inner<<" max_state_error="<<error
        <<" version="<<clock.last_version()<<'\n';
}



void test_rz_native_coordinates()
{
    using GridMetrics::GeometrySemantics;
    constexpr auto rz=GeometrySemantics::AxisymmetricRz;
    for (const auto [radius,z] : std::array<std::pair<double,double>,5>{
        {{0.,-4.},{0.,4.},{3.,-4.},{3.,4.},{1.e150,-1.e150}}}) {
        const auto p=Grid::PhysicalCoordsFromNative(2,"cylindrical",radius,z,0.,rz);
        if(p.x!=radius || p.y!=0. || p.z!=z || p.r_cy!=radius || p.z_cy!=z ||
           p.phi_cy!=0. || p.phi!=0. || p.r!=std::hypot(radius,z) ||
           p.theta!=std::atan2(radius,z))
            throw std::runtime_error("RZ native coordinate expansion mismatch");
    }
    Grid grid(amr::MAX_NG,0.,2.,-10.,10.,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";
    grid.InitializeTopology(rz); // z length >2pi is valid; not an angle.
    if(grid.GetAxisNames(rz)!=std::vector<std::string>{"r_cy","z_cy"})
        throw std::runtime_error("RZ native axes mismatch");
    for(int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i) {
        const auto p=grid.GetPhysicalCoords(i,j,grid.Ks(),rz);
        if(p.r_cy!=grid.GetCellCenterX(i) || p.z_cy!=grid.GetCellCenterY(j) ||
           p.z!=p.z_cy || p.r!=std::hypot(p.r_cy,p.z_cy))
            throw std::runtime_error("RZ native cell center mismatch");
    }
    // Same physical J/W formula, independently reduced at the regular axis.
    // Invalid native controls must leave every caller field/species untouched.
    const auto metric=GridMetrics::make_geometry_view(grid,rz);
    const int ai=grid.Is(),aj=grid.Js();
    double increment=41.;
    if(!GridMetrics::Rz::AngularFluxIncrement(metric,0,ai,aj,2.,3.,.01,increment)
        ||std::abs(increment-(-.09/grid.dx1))>3.e-14*std::max(1.,std::abs(increment)))
        throw std::runtime_error("RZ conditioned regular-axis torque integral changed");
    if(!GridMetrics::Rz::AngularFluxIncrement(metric,1,ai,aj,2.,2.,.01,increment)
        ||increment!=0.)throw std::runtime_error("RZ identical axial torque does not cancel exactly");
    for(double invalid_dt:{-1.,std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
        increment=41.;
        if(GridMetrics::Rz::AngularFluxIncrement(metric,0,ai,aj,2.,3.,invalid_dt,increment)
            ||increment!=41.)throw std::runtime_error("RZ invalid timestep partially published");
    }
    const TimeIntegration::NativeAngularDivergence absent{nullptr,0,ai,aj};
    const FluidVector sentinel{41.,43.,47.,53.,59.};auto delta=sentinel;
    double species_delta=61.,species_flux=1.;
    if(TimeIntegration::accumulate_cell_divergence(sentinel,sentinel,
        &species_flux,&species_flux,1,1,1.,1.,1.,.01,delta,&species_delta,&absent)
        ||delta.rho!=sentinel.rho||delta.mom_u!=sentinel.mom_u
        ||delta.mom_v!=sentinel.mom_v||delta.mom_w!=sentinel.mom_w
        ||delta.eng!=sentinel.eng||species_delta!=61.)
        throw std::runtime_error("RZ invalid divergence changed output before rejection");
    bool rejected=false;
    try {grid.InitializeTopology();} catch(const std::invalid_argument&) {rejected=true;}
    if(!rejected) throw std::runtime_error("legacy polar angle guard was removed");
    // Exact legacy coordinate witnesses and 3D cylindrical mapping.
    const auto old=Grid::PhysicalCoordsFromNative(2,"cylindrical",3.,.5);
    if(old.z_cy!=0. || old.phi_cy!=.5 || old.x!=3.*std::cos(.5) ||
       old.y!=3.*std::sin(.5))
        throw std::runtime_error("legacy cylindrical polar mapping changed");
    const auto three=Grid::PhysicalCoordsFromNative(3,"cylindrical",3.,-4.,.5);
    if(three.r_cy!=3. || three.z_cy!=-4. || three.phi_cy!=.5 || three.r!=5.)
        throw std::runtime_error("3D cylindrical mapping changed");
    for(const auto [dim,geometry] : std::array<std::pair<int,const char*>,4>{
        {{1,"cylindrical"},{3,"cylindrical"},{2,"cartesian"},{2,"spherical"}}}) {
        rejected=false;
        try {(void)Grid::PhysicalCoordsFromNative(dim,geometry,1.,2.,0.,rz);}
        catch(const std::invalid_argument&) {rejected=true;}
        if(!rejected) throw std::runtime_error("invalid RZ coordinate profile accepted");
    }
    std::cout<<"RZ_NATIVE_COORDINATES physical/native/axes/domain/legacy PASS\n";
}

/** Independent native solid-body means from polynomial antiderivatives.
 * rho=Omega=1, ur=0, uz=v; V weight |r|, W weight r^2.
 */
FluidVector selected_rotation_mean(double lo,double hi,double internal,double axial)
{
    const long double left=lo,right=hi,sign=hi<=0.?-1.L:1.L;
    const long double v=sign*(right*right-left*left)/2.L;
    const long double w=(right*right*right-left*left*left)/3.L;
    const long double inertia=(right*right*right*right-left*left*left*left)/4.L;
    return {1.,0.,axial,static_cast<double>(inertia/w),
        static_cast<double>(internal+.5L*axial*axial+sign*inertia/(2.L*v))};
}

/** Real logical Grid fixture; bound mode binds its actual level-zero root. */
Grid selected_rotation_grid(bool bound)
{
    Grid grid(amr::MAX_NG,0.,16.,-8.,8.,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";
    if(bound) {
        grid.dyadic_identity.bound=true;
        grid.dyadic_identity.root_lower={0.,-8.};
        grid.dyadic_identity.root_upper={16.,8.};
        grid.dyadic_identity.root_blocks={1,1};
        grid.dyadic_identity.level=0;grid.dyadic_identity.logical={0,0};
    }
    grid.InitializeTopology(GridMetrics::GeometrySemantics::AxisymmetricRz);
    return grid;
}

/** Populate all real ghosts by the same independent physical antiderivatives. */
void selected_fill_rotation(FluidState& state,const Grid& grid,
    const std::vector<double>& fractions,double axial,bool warm=false)
{
    state.Preallocate(grid.GetTotalSize());state.InitSpecies(static_cast<int>(fractions.size()));
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
        // A non-affine monotone warm axial stencil distinguishes limiter types.
        const double e=warm?100.+(j<6?j:j==6?6.:6.+2.*(j-6)):1./64.;
        const int index=grid.GetIndex(i,j);
        state.set(index,selected_rotation_mean(grid.GetFacePosL(i),grid.GetFacePosR(i),e,axial));
        for(int s=0;s<state.GetNumSpecies();++s)state.X(s,index)=fractions[s];
    }
}

/** Point bundle EOS gate and analytic baseline independent of reconstruction. */
void selected_check_pcm(const RzSelectedReconstruction::FaceBundles& bundle,
    const double* q,const std::vector<double>& fractions,const IdealGas& eos,double axial)
{
    if(bundle.status!=arch::state::Status::valid)
        throw std::runtime_error("Native selected PCM rejected independent cold rotation");
    const int count=static_cast<int>(fractions.size());
    for(int d=0;d<2;++d) {
        const auto& donor=bundle.donor[d];
        if(donor.theta!=1.)throw std::runtime_error("Native PCM altered its exact baseline ray");
        for(int n=0;n<donor.node_count;++n) {
            const double r=donor.radius[n];const auto& point=donor.point[n];
            close(point.rho,1.,"Selected PCM independent rho");
            close(point.mom_u,0.,"Selected PCM independent radial momentum");
            close(point.mom_v,axial,"Selected PCM independent axial momentum");
            close(point.mom_w,r,"Selected PCM independent physical swirl");
            close(point.eng,1./64.+.5*axial*axial+.5*r*r,"Selected PCM independent physical energy");
            std::vector<double> x(count);double sum=0.;
            for(int s=0;s<count;++s) {
                x[s]=q[(d*8+n)*count+s]/point.rho;sum+=x[s];
                close(x[s],fractions[s],"Selected PCM changed original species");
                if(fractions[s]==0.&&x[s]!=0.)throw std::runtime_error("Selected PCM invented a zero species");
                if(fractions[s]>0.&&!(x[s]>0.))throw std::runtime_error("Selected PCM lost a positive trace");
            }
            if(arch::state::validate_eos(point,x.data(),count,{},eos)!=arch::state::Status::valid)
                throw std::runtime_error("Selected PCM actual IdealGas rejected physical node");
            close(eos.get_pressure(point,x.data()),(1.4-1.)/64.,"Selected PCM analytic pressure");
            double original=0.;for(double value:fractions)original+=value;
            if(std::abs(sum-original)>32.*std::numeric_limits<double>::epsilon()*original)
                throw std::runtime_error("Selected PCM changed original near-one alpha");
            if(original>1.+8.*std::numeric_limits<double>::epsilon()
                &&!(sum>1.+8.*std::numeric_limits<double>::epsilon()))
                throw std::runtime_error("Selected PCM normalized original near-one alpha");
        }
    }
}

/** Actual IdealGas with observable required queries and a documented high probe.
 * Required rejection acts only in pressure; probe rejection leaves required
 * baseline EOS intact. It is an owner failure-injection fixture, not fake EOS.
 */
struct SelectedObservedEos {
    const IdealGas& eos;
    bool reject_probe=false,reject_required=false;
    double rejected_internal=0.;
    mutable int pressures=0,probes=0;
    double get_temperature(double rho,double e,const double* x) const {
        return eos.get_temperature(rho,e,x);
    }
    double get_pressure(const FluidVector& u,const double* x) const {
        ++pressures;
        const double e=arch::state::recover(u).internal;
        if(reject_required&&std::abs(e-rejected_internal)<=2.e-12*std::max(1.,std::abs(rejected_internal)))
            throw std::runtime_error("Selected required actual IdealGas pressure fixture rejection");
        return eos.get_pressure(u,x);
    }
    double get_sound_speed(const FluidVector& u,double p,const double* x) const {
        return eos.get_sound_speed(u,p,x);
    }
    // Keep the actual required IdealGas inverse interface complete. The shared
    // probe helper's later ordinary return is compiled even when its earlier
    // optional probe branch is selected; only the probe below injects failure.
    double get_total_energy_primitive(double rho,double u,double v,double w,double p,const double* x) const {
        return eos.get_total_energy_primitive(rho,u,v,w,p,x);
    }
    double probe_total_energy_primitive(double rho,double u,double v,double w,double p,const double* x) const {
        ++probes;
        return reject_probe?std::numeric_limits<double>::quiet_NaN():eos.get_total_energy_primitive(rho,u,v,w,p,x);
    }
};

/** Direct selected owner tests; not a method-order or coupled-stage proof. */
void test_rz_selected_face_bundles()
{
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr double axial=1./8.;
    for(bool bound:{false,true})for(int species_count:{2,13}) {
        SpeciesManager species;
        for(int s=0;s<species_count;++s)species.add_species("selected"+std::to_string(s),1.,1.,1.4,3.);
        IdealGas eos(1.4,species);
        std::vector<double> fractions(species_count,0.);
        fractions[0]=.2+16.*std::numeric_limits<double>::epsilon();fractions[1]=.8;
        if(species_count==13){fractions[1]=.3;fractions[2]=.5;fractions[3]=1.e-30;}
        const Grid grid=selected_rotation_grid(bound);FluidState state;
        selected_fill_rotation(state,grid,fractions,axial);
        const auto read=[&](int index){return state.get(index);};
        const auto fraction=[&](int s,int index){return state.X(s,index);};
        const int index=grid.GetIndex(grid.Is(),grid.Js());
        if(arch::state::recover(state.get(index)).status!=arch::state::Status::unresolved_energy)
            throw std::runtime_error("Selected cold fixture lost raw-mean unresolved diagnostic");
        close(state.eng[index]-.5*(state.mom_w[index]*state.mom_w[index]+axial*axial),
            -1./64.,"Selected raw mean negative energy antiderivative");
        for(int direction:{0,1}) {
            RzSelectedReconstruction::Context context{GridMetrics::make_geometry_view(grid,rz),
                grid.GetTotalX(),grid.GetTotalY(),grid.Is(),grid.Js(),direction,species_count,{}};
            std::vector<double> q(16*species_count,-17.),scratch(19*species_count);
            const auto bundle=RzSelectedReconstruction::reconstruct_face<PCMReconstruction>(
                read,fraction,context,eos,q.data(),scratch.data(),scratch.size());
            if(bundle.donor[0].node_count!=(direction==0?6:8))
                throw std::runtime_error("Selected PCM wrong physical node layout");
            selected_check_pcm(bundle,q.data(),fractions,eos,axial);
            auto bad=context;bad.bounds.internal_min=1.;
            std::fill(q.begin(),q.end(),-17.);
            const auto failure=RzSelectedReconstruction::reconstruct_face<PCMReconstruction>(
                read,fraction,bad,eos,q.data(),scratch.data(),scratch.size());
            if(failure.status==arch::state::Status::valid
                ||std::any_of(q.begin(),q.end(),[](double v){return v!=-17.;}))
                throw std::runtime_error("Selected required failure published rhoX output");
        }
    }
    SpeciesManager species;species.add_species("selected",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);const Grid grid=selected_rotation_grid(true);FluidState state;
    selected_fill_rotation(state,grid,{1.},0.,true);
    const auto read=[&](int index){return state.get(index);};
    const auto fraction=[&](int s,int index){return state.X(s,index);};
    // Direct Native scalar entry must receive configured bounds explicitly;
    // the real full face traversal receives them through the stage owner.
    for(int fault=0;fault<4;++fault) {
        arch::state::Bounds bounds{};
        if(fault==1)bounds.density=2.;
        if(fault==2)bounds.internal_min=200.;
        if(fault==3)bounds.internal_max=1.;
        FluidVector left(42.,43.,44.,45.,46.),right=left;
        double xl=-17.,xr=-17.,work=0.;bool rejected=false;
        try {AMRInterfaceReconstruction::reconstruct_face<PCMReconstruction>(
            state,eos,grid,0,grid.Is(),grid.Js(),0,grid.GetIndex(grid.Is(),grid.Js()),
            1,1,&xl,&xr,&work,left,right,rz,fault==0?nullptr:&bounds);}
        catch(const std::invalid_argument&){rejected=true;}
        catch(const std::runtime_error&){rejected=true;}
        if(!rejected||left.rho!=42.||left.eng!=46.||right.rho!=42.||right.eng!=46.
            ||xl!=-17.||xr!=-17.)
            throw std::runtime_error("Direct Native trace omitted configured bounds or published rejected outputs");
    }
    RzSelectedReconstruction::Context context{GridMetrics::make_geometry_view(grid,rz),
        grid.GetTotalX(),grid.GetTotalY(),grid.Is(),6,1,1,{}};
    std::vector<double> qm(16),qs(16),workspace(19);
    const auto minmod=RzSelectedReconstruction::reconstruct_face<MusclReconstruction<MinMod>>(
        read,fraction,context,eos,qm.data(),workspace.data(),workspace.size());
    const auto superbee=RzSelectedReconstruction::reconstruct_face<MusclReconstruction<SuperBee>>(
        read,fraction,context,eos,qs.data(),workspace.data(),workspace.size());
    if(minmod.status!=arch::state::Status::valid||superbee.status!=arch::state::Status::valid
        ||minmod.donor[0].theta!=1.||superbee.donor[0].theta!=1.)
        throw std::runtime_error("Selected warm MUSCL real policy stencil rejected");
    close(minmod.donor[0].point[0].eng-superbee.donor[0].point[0].eng,.5,
        "Native selected MinMod and SuperBee silently share one method");
    SelectedObservedEos observed{eos};
    std::vector<double> q(16,-17.);
    const auto ppm=RzSelectedReconstruction::reconstruct_face<PPMReconstruction>(
        read,fraction,context,observed,q.data(),workspace.data(),workspace.size());
    if(ppm.status!=arch::state::Status::valid||observed.probes!=16||observed.pressures<60)
        throw std::runtime_error("Native PPM skipped actual six-source pressure or both-trace probes");
    SelectedObservedEos high_failure{eos,true};
    const auto fallback=RzSelectedReconstruction::reconstruct_face<PPMReconstruction>(
        read,fraction,context,high_failure,q.data(),workspace.data(),workspace.size());
    if(fallback.status!=arch::state::Status::valid||fallback.donor[0].theta!=0.
        ||fallback.donor[1].theta!=0.||high_failure.probes!=16)
        throw std::runtime_error("Native PPM high inverse failure did not contract whole bundles");
    for(int d=0;d<2;++d)for(int n=0;n<8;++n) {
        const double r=fallback.donor[d].radius[n];const double e=d==0?106.:108.;
        close(fallback.donor[d].point[n].eng,e+.5*r*r,"PPM baseline fallback analytic energy");
        close(fallback.donor[d].point[n].mom_w,r,"PPM fallback changed swirl independently");
        close(q[d*8+n],1.,"PPM fallback changed species independently");
    }
    // Thirteen actual materials: N-1 selected profiles plus a dependent row.
    // Axial MUSCL endpoints independently preserve each owning q mean.
    SpeciesManager many_species;
    for(int k=0;k<13;++k)many_species.add_species("many"+std::to_string(k),1.,1.,1.4,3.);
    IdealGas many_eos(1.4,many_species);FluidState many;
    std::vector<double> initial(13,0.);initial[0]=.2;initial[1]=.3;initial[2]=.5;
    selected_fill_rotation(many,grid,initial,0.,true);
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
        const int cell=grid.GetIndex(i,j);
        many.X(0,cell)=(j<6?.1:j==6?.2:.25)+16.*std::numeric_limits<double>::epsilon();
        many.X(1,cell)=j<6?.2:j==6?.3:.45;
        many.X(2,cell)=j<6?.7:j==6?.5:.3;
        many.X(3,cell)=1.e-30;
    }
    auto many_context=context;many_context.species=13;
    std::vector<double> manyq(16*13,-17.),manywork(19*13);
    const auto manyread=[&](int cell){return many.get(cell);};
    const auto manyfraction=[&](int k,int cell){return many.X(k,cell);};
    const auto manybundle=RzSelectedReconstruction::reconstruct_face<MusclReconstruction<MinMod>>(
        manyread,manyfraction,many_context,many_eos,manyq.data(),manywork.data(),manywork.size());
    if(manybundle.status!=arch::state::Status::valid)
        throw std::runtime_error("Selected actual thirteen-species MUSCL rejected");
    for(int d=0;d<2;++d)for(int n=0;n<4;++n) {
        const int cell=grid.GetIndex(grid.Is(),6+d);
        double lower_sum=0.,upper_sum=0.;
        for(int k=0;k<13;++k) {
            const double low=manyq[(d*8+n)*13+k],high=manyq[(d*8+4+n)*13+k];
            close(.5*low+.5*high,many.X(k,cell),"Selected rhoX traces changed actual owning mean");
            lower_sum+=low;upper_sum+=high;
            if(k==3&&(!(low>0.)||!(high>0.)))
                throw std::runtime_error("Selected thirteen-species lost positive trace");
            if(k>=4&&(low!=0.||high!=0.))
                throw std::runtime_error("Selected thirteen-species invented zero trace");
        }
        if(!(lower_sum>1.+8.*std::numeric_limits<double>::epsilon())
            ||!(upper_sum>1.+8.*std::numeric_limits<double>::epsilon()))
            throw std::runtime_error("Selected high rhoX profiles normalized original alpha");
        const double r=manybundle.donor[d].radius[n],e=d==0?106.:108.;
        close(.5*manybundle.donor[d].point[n].eng+.5*manybundle.donor[d].point[4+n].eng,
            e+.5*r*r,"Selected fluid traces changed independent owning energy mean");
    }
    SelectedObservedEos required_failure{eos,false,true,112.};
    std::fill(q.begin(),q.end(),-17.);bool threw=false;
    try {(void)RzSelectedReconstruction::reconstruct_face<PPMReconstruction>(
        read,fraction,context,required_failure,q.data(),workspace.data(),workspace.size());}
    catch(const std::runtime_error& error){threw=std::string(error.what())==
        "Selected required actual IdealGas pressure fixture rejection";}
    if(!threw||std::any_of(q.begin(),q.end(),[](double v){return v!=-17.;}))
        throw std::runtime_error("Native PPM required wide EOS failure became a fallback/publication");
}

/** Actual sweep witness: physical time integration is outside this test.
 * Independent integrals on [0,1]: <r>_W=3/4, <r^2/2>_V=1/4.
 * Native FluxSweep wiring and actual physical_bounds cache dependency must be
 * integrated before Root compiles/runs this candidate; no current pass implied.
 */
void test_rz_selected_pcm_axial_flux()
{
    const Grid grid=selected_rotation_grid(true);FluidState state;
    constexpr double velocity=1./8.,e0=1./64.;
    selected_fill_rotation(state,grid,{1.},velocity);
    SpeciesManager species;species.add_species("selected",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    std::vector<FluidVector> flux(grid.GetTotalSize());std::vector<double> species_flux(grid.GetTotalSize());
    FluxAdmissibility::MeanThermoCache cache;cache.reset(grid.GetTotalSize());
    cache.geometry_semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
    cache.physical_bounds={};
    FluxHLLC<PCMReconstruction>::compute_fluxes(state,eos,grid,flux,species_flux,1,0.,&cache);
    const int face=grid.GetIndex(grid.Is(),grid.Js())+grid.stride_y;
    close(flux[face].rho,velocity,"Native PCM axial independent mass flux");
    close(flux[face].mom_w,3.*velocity/4.,"Native axial physical phi flux requires W average");
    close(flux[face].eng,velocity*(1.4*e0+1./4.+.5*velocity*velocity),
        "Native axial energy flux requires V antiderivative");
    close(species_flux[face],velocity,"Native axial species flux independent mass");
    for(int j=grid.Js()-1;j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
        const auto f=flux[grid.GetIndex(i,j)+grid.stride_y];
        if(!std::isfinite(f.rho)||!std::isfinite(f.mom_u)||!std::isfinite(f.mom_v)
            ||!std::isfinite(f.mom_w)||!std::isfinite(f.eng))
            throw std::runtime_error("Actual native PCM axial sweep produced invalid physical flux");
    }
}

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

int main(int argc,char** argv)
{
    if(argc==2 && std::string(argv[1])=="rz-applied-torque-audit") {
        try {
            for(int direction:{0,1})for(bool open:{false,true})for(double phi:{-.025,.025}) {
                test_rz_rotating_boundary_budget<SolverEuler>(direction,1.,open,phi);
                test_rz_rotating_boundary_budget<SolverRK2>(direction,1.,open,phi);
                test_rz_rotating_boundary_budget<SolverRK3>(direction,1.,open,phi);
            }
            return 0;
        } catch(const std::invalid_argument& error) {
            if(std::string(error.what())!="RZ gravity requires authoritative finite-ring contract") throw;
            std::cerr<<"RZ_APPLIED_TORQUE_SCIENCE_GATE=NOT_CLEARED "
                "finding=RZ-EXT-TORQUE-GATE-01 planned_cases=24 completed_cases=0\n";
            return 2;
        }
    }
    if(argc==2 && std::string(argv[1])=="rz-equilibrium-audit")
        return audit_rz_rotating_equilibrium();
    if(argc==2 && std::string(argv[1])=="rz-ppm-equilibrium-audit")
        return audit_rz_rotating_equilibrium<PPMReconstruction>("PPM");
    // Both selected policies use the independent native polynomial reference,
    // seven unchanged residual norms and the original rounding/order gates.
    if(audit_rz_rotating_equilibrium()!=0
        ||audit_rz_rotating_equilibrium<PPMReconstruction>("PPM")!=0) return 2;
    test_rz_selected_face_bundles();
    test_rz_selected_pcm_axial_flux();
    test_rz_angular_measures();
    test_rz_torque_divergence_budget();
    test_rz_native_coordinates();
    for(int direction:{0,1})for(double inner:{0.,1.}) {
        test_rz_scheduled_hydro<SolverEuler>(direction,inner);
        test_rz_scheduled_hydro<SolverRK2>(direction,inner);
        test_rz_scheduled_hydro<SolverRK3>(direction,inner);
    }
    for(int direction:{0,1})for(double inner:{0.,1.})
        test_rz_mixed_hydro_stage(direction,inner);
    for(int direction:{0,1})for(double inner:{0.,1.}) {
        test_rz_rotating_boundary_budget<SolverEuler>(direction,inner);
        test_rz_rotating_boundary_budget<SolverRK2>(direction,inner);
        test_rz_rotating_boundary_budget<SolverRK3>(direction,inner);
    }
    for(int direction:{0,1})for(double inner:{0.,1.}) {
        test_rz_rotating_boundary_budget<SolverEuler>(direction,inner,true);
        test_rz_rotating_boundary_budget<SolverRK2>(direction,inner,true);
        test_rz_rotating_boundary_budget<SolverRK3>(direction,inner,true);
    }
    test_rz_host_cfl();
    test_rz_host_hydro();
    using namespace GridMetrics;
    const double conditioning = CurvilinearMetricCases::conditioning_error();
    if (!std::isfinite(conditioning) || conditioning > 2.e-12)
        throw std::runtime_error("independent thin-shell/polar measures");
    std::cout << "INDEPENDENT_METRIC_MAX_RELATIVE_ERROR=" << conditioning << '\n';
    // Same existing arithmetic metric gate; no new production science budget.
    const double rz_conditioning = RzMetricCases::conditioning_error();
    RzMetricCases::cell_average_samples();
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
        close(delta.mom_w,29.,"RZ duplicated phi curvature source");
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
        close(existing.mom_w-29.,-dt*2.*3.*5.*inv,"Legacy full cylindrical phi source");
        if (existing.mom_u!=delta.mom_u || existing.mom_v!=delta.mom_v
            || delta.mom_w!=29.)
            throw std::runtime_error("RZ torque source changed unrelated cylindrical contributions");
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
    // Independent axisymmetric polynomial witness:
    // vr=(a+b*z)*r, vz=c*r*r+d*z, vphi=(w+q*z)*r.
    // div=2(a+b*z)+d; curl=(-q*r, (b-2c)*r, 2(w+q*z)).
    for (double left : {0.,1.,4.}) {
        auto grid=make_rz_geometry_view(make_geometry_view(
            Geometry::Cylindrical,2,{left,-1.,0.},{.25,.5,0.}));
        grid.ng=1;grid.stride_y=5;grid.stride_z=25;grid.total_size=25;
        std::vector<double> vr(25),vz(25),vp(25);
        const double a=.5,b=.25,c=-.75,d=1.5,w=2.,q=.5;
        for (int j=0;j<5;++j) for (int i=0;i<5;++i) {
            const int cell=grid.GetIndex(i,j);
            const double radius=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
            vr[cell]=(a+b*z)*radius;vz[cell]=c*radius*radius+d*z;vp[cell]=(w+q*z)*radius;
        }
        for (int j : {1,2,3}) for (int i : {1,2,3}) {
            const double radius=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
            const auto value=VelocityDiagnostics::evaluate(grid,vr,vz,vp,i,j,0);
            close(value.divergence,2.*(a+b*z)+d,"RZ analytic finite-volume divergence");
            close(value.vorticity,std::sqrt(q*q*radius*radius+(b-2.*c)*(b-2.*c)*radius*radius
                +4.*(w+q*z)*(w+q*z)),"RZ analytic axisymmetric curl");
        }
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
    // The original radial/axial connection remains unchanged. The phi
    // connection now belongs to the symmetric torque divergence below and
    // must not also be applied as the former vector-Laplacian cell source.
    for (double left : {0.,1.,4.}) {
        auto grid=make_rz_geometry_view(make_geometry_view(
            Geometry::Cylindrical,2,{left,-1.,0.},{.25,.5,0.}));
        grid.ng=1;grid.stride_y=3;grid.stride_z=9;grid.total_size=9;
        std::vector<FluidVector> states(9);
        for (int j=0;j<3;++j) for (int i=0;i<3;++i) {
            const double r=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
            states[grid.GetIndex(i,j)]={2.,2.*r,6.*z,4.*r,1000.};
        }
        const auto cfg=DiffFlux::make_diffusion_config_view(config);
        DiffFlux::DiffusionCoefficients coefficients{};
        coefficients.nu_visc=ViscousGeometryCases::viscosity;
        const double composition[]{1.};
        const int cell=grid.GetIndex(1,1);
        FluidVector delta{};
        // The actual native thermal closure needs rho at this signed annulus
        // and its two radial neighbors. These are real positive-density cells
        // in the same 3x3 layout, including the reflected annulus when left=0.
        // Axisymmetry still forbids any inactive phi-neighbor access; retain a
        // strict reader whitelist rather than forbidding legitimate radial rho.
        std::array<int,3> radial_reads{};
        const auto read=[&](int index) {
            if(index<cell-1 || index>cell+1)
                throw std::runtime_error("RZ viscous source accessed inactive phi neighbour");
            ++radial_reads[static_cast<std::size_t>(index-(cell-1))];
            return states.at(static_cast<std::size_t>(index));
        };
        const auto status=DiffFlux::evaluate_geometric_diffusion_cell(
            states[cell],composition,eos,species.get_host_view(),cfg,grid,1,1,0,1.,
            nullptr,nullptr,delta,read);
        if (!status.valid || !status.active)
            throw std::runtime_error("RZ viscous source rejected the actual thermal density stencil");
        if(radial_reads[0]==0 || radial_reads[1]==0 || radial_reads[2]==0)
            throw std::runtime_error("RZ viscous source omitted its actual radial density stencil");
        const double r=grid.GetCellCenterX(1),inv=2./(left+left+.25);
        close(delta.mom_u,-2.*coefficients.nu_visc*inv,"RZ radial viscous connection");
        close(delta.mom_w,0.,"RZ duplicated azimuthal viscous connection");
        if (delta.mom_v!=0. || delta.rho!=0. || delta.eng!=0.)
            throw std::runtime_error("RZ viscous source changed z/mass/energy");
        close(DiffFlux::viscous_source_stability_rate(coefficients.nu_visc,grid,1,1),
            coefficients.nu_visc*inv/r,"RZ unresolved phi row bound");
        if (delta.rho!=0.) throw std::runtime_error("RZ viscosity changed mass");
    }
    // Complete Host operator with native padded storage and explicit RZ chart.
    RzViscousCases::native_thermodynamic_closure();
    RzReconstructionCases::native_profile();
    test_rz_supported_density();
    RzViscousCases::azimuthal_operator();
    ViscousGeometryCases::newtonian_constitutive();
    ViscousGeometryCases::newtonian_covariant_gradient();
    ViscousGeometryCases::newtonian_paired_faces();
    test_cartesian_real_diffusion_boundaries();
    ViscousGeometryCases::convergence("cpu", evaluate);
    ViscousGeometryCases::radial_origin("cpu", evaluate);
    ViscousGeometryCases::density_stability("cpu", evaluate);
}
