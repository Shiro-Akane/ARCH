/**
 * @file NativeRzBoundaryMath.h
 * @brief Allocation-free positive-r RZ reflecting law shared by Host and CUDA.
 *
 * Workflow:
 * 1. Borrow the actual geometry, logical extents and mirrored source/target.
 * 2. Select the original three real density observations and validate U/X.
 * 3. Build the existing density/inertia closure; map each of the original
 *    eight target Gauss points into its actual positive source support.
 * 4. Check each source physical primitive with the selected EOS, change only
 *    the normal momentum sign, and check the reflected physical point.
 * 5. Reuse the sole V/W conserved integral and scaled density-weighted Xi
 *    ratio. Return an unpublished candidate or an explicit failure category.
 *
 * m_phi=J/W; rho, m_r, m_z, E and rho*Xi use V. At a radial wall R,
 * v_phi,g(r)=Omega*(2R-r). Neither raw native copying nor a thermal correction
 * implements that law. This leaf allocates nothing, repairs nothing and owns
 * no ghost/stage/EOS publication, callback ordering or topology authority.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "amr/exchange/BoundaryPlan.h"
#include "core/ArchPortability.h"
#include "data/FluidState.h"
#include "grid/GridMetrics.h"
#include "numerics/state/RzCellAverage.h"
#include "numerics/state/RzNativeClosure.h"
#include "numerics/state/StateAdmissibility.h"
#include "physics/boundary/BoundaryFlux.h"

namespace arch::boundary::native_rz_math {

/** Select reflecting physical faces from the actual bound logical authority.
 * Workflow: validate the shared canonical grid and original plan layout;
 * retain only a configured Reflecting token on a dyadic root edge; exclude
 * the regular radial axis. Internal/periodic faces cannot become Hydro walls.
 * The Host BC owner authenticates its root/config first; the CUDA factory uses
 * this same pure selector before caching the immutable value for its generation.
 * This value alone grants neither completed ghosts nor thermal acceptance.
 */
inline HydroBoundaryView bound_reflecting_faces(
    const BoundaryPlan& plan, const GridMetrics::GeometryView& geometry)
{
    const auto& input = plan.input();
    const auto& identity = geometry.dyadic_identity;
    if (geometry.semantics != GridMetrics::GeometrySemantics::AxisymmetricRz
        || !identity.bound || !GridMetrics::matches_identity(geometry)
        || input.dimension != 2 || input.ghost_depth != geometry.ng
        || input.active_extent[0] != amr::BLOCK_NX
        || input.active_extent[1] != amr::BLOCK_NY
        || input.active_extent[2] != 1)
        throw std::invalid_argument("Native reflecting plan differs from the actual bound grid");
    HydroBoundaryView result;
    for (int axis = 0; axis < 2; ++axis) {
        for (int side = 0; side < 2; ++side) {
            const int face = 2 * axis + side;
            if (input.faces[face] != BoundaryType::Reflecting) continue;
            const bool physical = side
                ? std::uint64_t(identity.logical[axis]) + 1
                    == (std::uint64_t(identity.root_blocks[axis]) << identity.level)
                : identity.logical[axis] == 0;
            const double edge = side ? geometry.actual_block_upper[axis]
                : (axis == 0 ? geometry.x1_min : geometry.x2_min);
            result.reflecting[face] = physical && !(axis == 0 && edge == 0.);
        }
    }
    return result;
}

/** Numerical failures; invalid input and physical/integration failures differ. */
enum class Status : unsigned char {
    valid, invalid_request, invalid_cell, invalid_support, invalid_source_layout,
    insufficient_active_support, invalid_bounds_or_species, invalid_workspace,
    invalid_source_state, invalid_source_closure, invalid_point_closure,
    invalid_point_eos, invalid_point_thermal, invalid_point_composition,
    invalid_point_temperature, invalid_point_pressure, invalid_mapped_fraction,
    invalid_mapped_point, invalid_target_measures, invalid_reflected_eos,
    conserved_integration_failed, fraction_integration_failed, invalid_candidate
};

/** Actual logical extents/active intervals, separate from padded allocation. */
struct Context {
    GridMetrics::GeometryView geometry;
    int logical_nx=0,logical_ny=0;
    int radial_begin=0,radial_end=0,axial_begin=0,axial_end=0;
};

/** Mirrored cell request without Host callback data, storage or publication. */
struct Request {
    std::array<int,2> source{},destination{};
    BoundaryAxis axis=BoundaryAxis::X1;
    BoundarySide side=BoundarySide::Lower;
    int ghost_depth=0;
    BoundaryPurpose purpose=BoundaryPurpose::Hydro;
};

/** Real positive-cell support; negative axis ghosts use a different owner. */
struct CellSupport { double r_lower,r_upper,z_lower,z_upper; };

/** Scalar primitive snapshot; complete Xi stays in caller-owned storage. */
struct Primitive {
    double rho=0.,u=0.,v=0.,w=0.,temperature=0.,pressure=0.;
};

/** Contiguous scratch/source/support/eight-sample Xi and separate result Xi.
 * For N species, values has 10*N doubles: N source, N support, 8*N samples.
 * Result Xi has N doubles. The caller owns disjoint scratch/output and lends
 * immutable readers from its frozen candidate prefix; this is not a state view.
 */
struct Workspace {
    double* values=nullptr;
    std::size_t size=0;
    double* fractions=nullptr;
    std::size_t fraction_size=0;
};

/** Provisional fluid result; failed calls supply no usable default fluid. */
struct Result {
    FluidVector conserved;
    Status status;
    ARCH_INLINE explicit Result(Status failure=Status::invalid_candidate)
        :conserved(arch::state::invalid(),arch::state::invalid(),arch::state::invalid(),
                   arch::state::invalid(),arch::state::invalid()),status(failure) {}
    /** Mathematical/point-EOS success only, without a Runtime capability grant. */
    ARCH_INLINE bool valid() const {return status==Status::valid;}
};

/** Compute the scratch extent before allocation/launch, without a species cap. */
ARCH_INLINE bool workspace_extent(int species,std::size_t& extent)
{
    if(species<0||static_cast<std::size_t>(species)>std::numeric_limits<std::size_t>::max()/10)
        return false;
    extent=10*static_cast<std::size_t>(species);return true;
}

/** Check borrowed workspace extents; N=0 permits null without null arithmetic. */
ARCH_INLINE bool valid_workspace(const Workspace& workspace,int species)
{
    std::size_t extent=0;
    return workspace_extent(species,extent)&&workspace.size>=extent
        &&workspace.fraction_size>=static_cast<std::size_t>(species)
        &&(species==0||(workspace.values&&workspace.fractions));
}

/** Small integer clamp preserving the original std::clamp expression order. */
ARCH_INLINE int clamp_index(int value,int lower,int upper)
{
    return value<lower?lower:upper<value?upper:value;
}

/** Require actual positive logical source/target bounds before any state read. */
ARCH_INLINE Status support(const Context& context,const std::array<int,2>& cell,
                           CellSupport& result)
{
    if(cell[0]<0||cell[0]>=context.logical_nx||cell[1]<0||cell[1]>=context.logical_ny)
        return Status::invalid_cell;
    const auto& grid=context.geometry;
    result={grid.GetFacePosL(cell[0]),grid.GetFacePosR(cell[0]),
        grid.GetAxialFacePosL(cell[1]),grid.GetAxialFacePosR(cell[1])};
    if(!std::isfinite(result.r_lower)||!std::isfinite(result.r_upper)
       ||!std::isfinite(result.z_lower)||!std::isfinite(result.z_upper)
       ||result.r_lower<0.||!(result.r_upper>result.r_lower)
       ||!(result.z_upper>result.z_lower))return Status::invalid_support;
    return Status::valid;
}

/** Authenticate the existing normal mirror and true tangential donor policy. */
ARCH_INLINE Status validate_request(const Context& context,const Request& request)
{
    const auto& grid=context.geometry;
    const int axis=static_cast<int>(request.axis),depth=request.ghost_depth;
    if(grid.geometry!=GridMetrics::Geometry::Cylindrical||grid.dim!=2
       ||grid.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
       ||context.logical_nx<3||context.logical_ny<1||grid.ng<1
       ||!std::isfinite(grid.dx1)||!(grid.dx1>0.)
       ||!std::isfinite(grid.dx2)||!(grid.dx2>0.)||axis<0||axis>1
       ||depth<1||depth>grid.ng
       ||(request.side!=BoundarySide::Lower&&request.side!=BoundarySide::Upper)
       ||(request.purpose!=BoundaryPurpose::Hydro&&request.purpose!=BoundaryPurpose::Diffusion))
        return Status::invalid_request;
    const int lower=axis==0?context.radial_begin:context.axial_begin;
    const int upper=axis==0?context.radial_end:context.axial_end;
    if(lower<0||upper<=lower||upper>(axis==0?context.logical_nx:context.logical_ny)
       ||context.axial_begin<0||context.axial_end<=context.axial_begin
       ||context.axial_end>context.logical_ny)return Status::invalid_request;
    const bool left=request.side==BoundarySide::Lower;
    const int target=left?lower-depth:upper+depth-1;
    const int donor=left?lower+depth-1:upper-depth;
    if(request.destination[axis]!=target||request.source[axis]!=donor
       ||(axis==0&&request.source[1]!=clamp_index(request.destination[1],
           context.axial_begin,context.axial_end-1))
       ||(axis==1&&request.source[0]!=request.destination[0]))return Status::invalid_request;
    return Status::valid;
}

/** Select original three-density support from active or completed corner cells.
 * Active: begin=clamp(i-1,Is,Ie-3). A true completed axial-corner donor outside
 * that interval uses begin=clamp(i-1,0,logical_nx-3). Padded stride is never nx.
 */
ARCH_INLINE Status source_support_begin(const Context& context,int source_i,int& begin)
{
    const int nx=context.logical_nx,lower=context.radial_begin,upper=context.radial_end;
    const auto& grid=context.geometry;
    if(nx<3||grid.ng<1||grid.stride_y<nx||source_i<0||source_i>=nx
       ||lower<0||upper<lower||upper>nx)return Status::invalid_source_layout;
    if(source_i>=lower&&source_i<upper) {
        if(upper-lower<3)return Status::insufficient_active_support;
        begin=clamp_index(source_i-1,lower,upper-3);
    } else begin=clamp_index(source_i-1,0,nx-3);
    return Status::valid;
}

/** Map target fractions into their real donor, reflecting only the normal.
 * s=source_upper-f*source_width is 2*face-target for normal mirror cells;
 * source_lower+f*source_width retains the actual tangential cell fraction.
 */
ARCH_INLINE Status mapped_coordinate(double target,double target_lower,double target_upper,
    double source_lower,double source_upper,bool reflect,double& point)
{
    const double fraction=(target-target_lower)/(target_upper-target_lower);
    if(!std::isfinite(fraction)||fraction<0.||fraction>1.)return Status::invalid_mapped_fraction;
    point=reflect?source_upper-fraction*(source_upper-source_lower)
        :source_lower+fraction*(source_upper-source_lower);
    if(!std::isfinite(point)||point<source_lower||point>source_upper)
        return Status::invalid_mapped_point;
    return Status::valid;
}

/** Check a physical closure point and preserve the original primitive queries.
 * First validate_eos calls T/P/c on the selected EOS. Then the old primitive
 * snapshot sequence repeats recover/simplex and T/P, with positive finite
 * results required. Its stricter shared simplex check already implies the
 * Host snapshot's magnitude-scaled simplex bound. No native raw recovery is
 * used; EOS exceptions on Host propagate to the transaction owner unchanged.
 */
template<class Eos>
ARCH_INLINE Status physical_point(const RzThermodynamics::Cell& closure,double radius,
    const double* fractions,int species,const arch::state::Bounds& bounds,const Eos& eos,
    FluidVector& point,Primitive& primitive)
{
    if(!closure.valid()||!std::isfinite(radius)||radius<closure.density.lower
       ||radius>closure.density.upper)return Status::invalid_point_closure;
    point=RzThermodynamics::base_point(closure,radius);
    if(arch::state::validate_eos(point,fractions,species,bounds,eos)!=arch::state::Status::valid)
        return Status::invalid_point_eos;
    const auto kinematics=arch::state::recover(point);
    if(kinematics.status!=arch::state::Status::valid)return Status::invalid_point_thermal;
    if(arch::state::validate_composition(fractions,species,1)!=arch::state::Status::valid)
        return Status::invalid_point_composition;
    primitive.rho=point.rho;primitive.u=kinematics.u;primitive.v=kinematics.v;primitive.w=kinematics.w;
    primitive.temperature=eos.get_temperature(point.rho,kinematics.internal,fractions);
    if(!(primitive.temperature>0.)||!std::isfinite(primitive.temperature))
        return Status::invalid_point_temperature;
    primitive.pressure=eos.get_pressure(point,fractions);
    if(!(primitive.pressure>0.)||!std::isfinite(primitive.pressure))return Status::invalid_point_pressure;
    return Status::valid;
}

/** Check dz/V/W in the original order and borrow the original eight samples. */
ARCH_INLINE Status cell_samples(const CellSupport& target,
    std::array<GridMetrics::Rz::CellAverageSample,8>& samples)
{
    const double dz=target.z_upper-target.z_lower;
    const double volume=GridMetrics::Rz::CellVolume(target.r_lower,target.r_upper,dz);
    const double angular=GridMetrics::Rz::AngularMomentumMeasure(target.r_lower,target.r_upper,dz);
    if(!std::isfinite(volume)||!(volume>0.)||!std::isfinite(angular)||!(angular>0.))
        return Status::invalid_target_measures;
    samples=GridMetrics::Rz::CellAverageSamples(target.r_lower,target.r_upper,target.z_lower,target.z_upper);
    return Status::valid;
}

/** Immutable point-array reader, callable on either backend without captures. */
struct PointReader {
    const FluidVector* points;
    ARCH_INLINE FluidVector operator()(std::size_t sample) const {return points[sample];}
};

/** Immutable species-major eight-point reader; no premature rho*Xi product. */
struct SampleFractionReader {
    const double* fractions;
    int species;
    ARCH_INLINE double operator()(std::size_t sample) const {
        return fractions[static_cast<std::size_t>(species)*8+sample];
    }
};

/** Integrate the original V/W and scaled Xi sequence, then precheck the mean.
 * rho/mr/mz/E=sum(w_V U), mphi=sum(w_W rho*vphi), and
 * Xi=sum(w_V*rho*Xi)/sum(w_V*rho). Completed ghost EOS remains outside this leaf.
 */
ARCH_INLINE Result integrate_samples(const std::array<GridMetrics::Rz::CellAverageSample,8>& samples,
    const std::array<FluidVector,8>& points,const std::array<double,8>& weights,
    const std::array<double,8>& densities,const double* fractions,int species,
    const arch::state::Bounds& bounds,double* mean_fractions)
{
    const auto conserved=RzCellAverage::conserved_mean(samples,PointReader{points.data()});
    if(!conserved.valid())return Result(Status::conserved_integration_failed);
    for(int s=0;s<species;++s) {
        const auto averaged=RzCellAverage::fraction_mean(weights,densities,SampleFractionReader{fractions,s});
        if(!averaged.valid())return Result(Status::fraction_integration_failed);
        mean_fractions[s]=averaged.value;
    }
    if(RzThermodynamics::provisional_native_state(conserved.value,mean_fractions,species,1,bounds)
       !=arch::state::Status::valid)return Result(Status::invalid_candidate);
    Result result(Status::valid);result.conserved=conserved.value;return result;
}

/** Original reflecting law using only immutable readers and caller scratch.
 * The three support observations may include signed completed axis copies;
 * only physical source/target point supports must be positive r. No source or
 * destination field is written. Output Xi is copied only after full success;
 * a failed call may change scratch, never the caller's published state.
 */
template<class Eos,class StateReader,class FractionReader>
ARCH_INLINE Result reflect_cell(const Context& context,const Request& request,
    const arch::state::Bounds& bounds,int species,const Eos& eos,
    const StateReader& read,const FractionReader& fraction,const Workspace& workspace)
{
    auto status=validate_request(context,request);
    if(status!=Status::valid)return Result(status);
    CellSupport source{},target{};
    status=support(context,request.source,source);if(status!=Status::valid)return Result(status);
    status=support(context,request.destination,target);if(status!=Status::valid)return Result(status);
    if(!arch::state::valid_bounds(bounds)||species<0)return Result(Status::invalid_bounds_or_species);
    if(!valid_workspace(workspace,species))return Result(Status::invalid_workspace);
    const auto& geometry=context.geometry;
    const int source_index=geometry.GetIndex(request.source[0],request.source[1],0);
    int source_begin=0;
    status=source_support_begin(context,request.source[0],source_begin);
    if(status!=Status::valid)return Result(status);
    double* source_x=species?workspace.values:nullptr;
    double* support_x=species?workspace.values+species:nullptr;
    double* sample_x=species?workspace.values+2*static_cast<std::size_t>(species):nullptr;
    for(int s=0;s<species;++s)source_x[s]=fraction(s,source_index);
    for(int i=source_begin;i<source_begin+3;++i) {
        const int index=geometry.GetIndex(i,request.source[1],0);
        for(int s=0;s<species;++s)support_x[s]=fraction(s,index);
        if(RzThermodynamics::provisional_native_state(read(index),support_x,species,1,bounds)
           !=arch::state::Status::valid)return Result(Status::invalid_source_state);
    }
    const auto source_closure=RzThermodynamics::make_cell_supported(read,source_index,
        geometry,request.source[0],source_begin,bounds);
    if(!source_closure.valid())return Result(Status::invalid_source_closure);
    std::array<GridMetrics::Rz::CellAverageSample,8> samples{};
    status=cell_samples(target,samples);if(status!=Status::valid)return Result(status);
    std::array<FluidVector,8> points{};
    std::array<double,8> weights{},densities{};
    for(std::size_t k=0;k<samples.size();++k) {
        const auto& q=samples[k];double axial=0.,radius=0.;
        status=mapped_coordinate(q.axial,target.z_lower,target.z_upper,source.z_lower,
            source.z_upper,request.axis==BoundaryAxis::X2,axial);
        if(status!=Status::valid)return Result(status);
        status=mapped_coordinate(q.radius,target.r_lower,target.r_upper,source.r_lower,
            source.r_upper,request.axis==BoundaryAxis::X1,radius);
        if(status!=Status::valid)return Result(status);
        FluidVector reflected;Primitive primitive;
        status=physical_point(source_closure,radius,source_x,species,bounds,eos,reflected,primitive);
        if(status!=Status::valid)return Result(status);
        if(request.axis==BoundaryAxis::X1)
            reflected.mom_u*=reflection_sign(request.axis,BoundaryType::Reflecting,BoundaryFieldClass::MomentumX);
        else reflected.mom_v*=reflection_sign(request.axis,BoundaryType::Reflecting,BoundaryFieldClass::MomentumY);
        if(arch::state::validate_eos(reflected,source_x,species,bounds,eos)!=arch::state::Status::valid)
            return Result(Status::invalid_reflected_eos);
        points[k]=reflected;weights[k]=q.volume_weight;densities[k]=points[k].rho;
        for(int s=0;s<species;++s)sample_x[static_cast<std::size_t>(s)*8+k]=source_x[s];
    }
    // Support scratch is dead after closure construction; reuse it for Xi means.
    const auto result=integrate_samples(samples,points,weights,densities,sample_x,species,bounds,support_x);
    if(!result.valid())return result;
    for(int s=0;s<species;++s)workspace.fractions[s]=support_x[s];
    return result;
}

static_assert(std::is_trivially_copyable_v<Context>);
static_assert(std::is_trivially_copyable_v<Request>);
static_assert(std::is_trivially_copyable_v<Workspace>);

} // namespace arch::boundary::native_rz_math
