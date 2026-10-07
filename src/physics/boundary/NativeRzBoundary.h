/**
 * @file NativeRzBoundary.h
 * @brief Read-only Host point-law construction of native positive RZ ghosts.
 *
 * Workflow:
 * 1. Check actual logical source/target cells and the normal-depth mirror.
 * 2. Bind the source's true three-density numerical closure and cell Xi.
 * 3. User laws evaluate the actual center callback/resolver and retain face
 *    conditions. Built-in reflecting laws need no callback or synthetic data.
 * 4. None/direct-flux inheritance returns native U/X exactly, without target
 *    closure or cell integration. Full/state-changing laws sample all eight
 *    actual target points, map each to its own real donor cell, and use the
 *    same point resolver/EOS. Changing Diffusion borrows the true target base.
 * 5. Reflect actual source physical points with the sole normal sign recipe;
 *    target points retain rho/E/tangential momentum and complete composition.
 * 6. Reuse the sole V/W and density-weighted Xi integration, then return only
 *    a provisional native candidate. Runtime owns join/publication, signed
 *    axis completion, final exchange and full same-stage ghost/EOS acceptance.
 *
 * m_phi=J/W; other conserved quantities and rho*Xi use V. Source profiles are
 * numerical closures, not unknown-field certificates. No state is written,
 * no negative-r callback is folded/clipped, and no floor/heating is applied.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

#include "amr/exchange/BoundaryPlan.h"
#include "data/FluidState.h"
#include "data/GlobalDefs.h"
#include "data/UserTypes.h"
#include "grid/Grid.h"
#include "grid/GridMetrics.h"
#include "numerics/state/RzCellAverage.h"
#include "numerics/state/RzNativeClosure.h"
#include "numerics/state/StateAdmissibility.h"
#include "physics/boundary/BoundaryTypes.h"
#include "physics/boundary/PhysicalBoundary.h"
#include "physics/species/Species.h"

namespace arch::boundary {

/** Actual logical donor/target indices and the frozen physical center request. */
struct NativeRzBoundaryRequest {
    std::array<int,2> source{},destination{};
    BoundaryCoordinates coordinates;
};

namespace native_rz_detail {
/** Real positive-cell support; never infer it from padded allocation columns. */
struct CellSupport { double r_lower,r_upper,z_lower,z_upper; };

/** Read physical bounds from actual logical grid cells; reject negative r. */
inline CellSupport support(const Grid& grid,const std::array<int,2>& cell)
{
    if(cell[0]<0||cell[0]>=grid.GetTotalX()||cell[1]<0||cell[1]>=grid.GetTotalY())
        throw std::invalid_argument("native RZ boundary requires real logical source and target cells");
    // Use the same canonical axial faces as the actual grid measure. Gauss
    // samples and V/W below then share this exact logical-cell height.
    CellSupport result{grid.GetFacePosL(cell[0]),grid.GetFacePosR(cell[0]),
        grid.GetAxialFacePosL(cell[1]),grid.GetAxialFacePosR(cell[1])};
    if(!std::isfinite(result.r_lower)||!std::isfinite(result.r_upper)
       ||!std::isfinite(result.z_lower)||!std::isfinite(result.z_upper)
       ||result.r_lower<0.||!(result.r_upper>result.r_lower)
       ||!(result.z_upper>result.z_lower))
        throw std::invalid_argument("native RZ physical callback requires a positive radial cell with finite ordered bounds");
    return result;
}

/** Require the frozen donor policy: normal mirror and true tangent ownership. */
inline void validate_request(const Grid& grid,const NativeRzBoundaryRequest& request)
{
    const auto& c=request.coordinates;
    const int axis=static_cast<int>(c.axis),depth=c.ghost_depth;
    if(grid.GetTotalX()<3||grid.GetTotalY()<1||grid.ng<1
       ||!std::isfinite(grid.dx1)||!(grid.dx1>0.)
       ||!std::isfinite(grid.dx2)||!(grid.dx2>0.)
       ||c.dimension!=2||axis<0||axis>1||depth<1||depth>grid.ng
       ||(c.side!=BoundarySide::Lower&&c.side!=BoundarySide::Upper))
        throw std::invalid_argument("native RZ boundary requires an actual two-dimensional physical ghost request");
    if(c.purpose!=BoundaryPurpose::Hydro&&c.purpose!=BoundaryPurpose::Diffusion)
        throw std::invalid_argument("native RZ boundary supports only Hydro and Diffusion requests");
    const int lower=axis==0?grid.Is():grid.Js(),upper=axis==0?grid.Ie():grid.Je();
    const bool left=c.side==BoundarySide::Lower;
    const int target=left?lower-depth:upper+depth-1;
    const int donor=left?lower+depth-1:upper-depth;
    if(request.destination[axis]!=target||request.source[axis]!=donor
       ||(axis==0&&request.source[1]!=std::clamp(request.destination[1],grid.Js(),grid.Je()-1))
       ||(axis==1&&request.source[0]!=request.destination[0]))
        throw std::invalid_argument("native RZ boundary donor does not match its physical mirror and tangent cell");
}

/** Map a target fraction into its own true donor; reflect only the normal.
 * Normal mapping s=source_upper-f*(source_upper-source_lower) is mathematically
 * 2*face-target for mirrored cells. Tangents use source_lower+f*source_width;
 * both remain inside actual support, including clamped axial tangent donors.
 */
inline double mapped_coordinate(double target,double target_lower,double target_upper,
    double source_lower,double source_upper,bool reflect)
{
    const double fraction=(target-target_lower)/(target_upper-target_lower);
    if(!std::isfinite(fraction)||fraction<0.||fraction>1.)
        throw std::runtime_error("native RZ boundary target sample lies outside its actual cell");
    const double point=reflect?source_upper-fraction*(source_upper-source_lower)
        :source_lower+fraction*(source_upper-source_lower);
    if(!std::isfinite(point)||point<source_lower||point>source_upper)
        throw std::runtime_error("native RZ boundary mapped point lies outside its actual donor cell");
    return point;
}

/** Build actual target-tangent face/sample coordinates with unchanged epoch. */
inline BoundaryCoordinates point_coordinates(const Grid& grid,
    const NativeRzBoundaryRequest& request,double radius,double axial)
{
    std::array<double,3> ghost{radius,axial,0.},face=ghost;
    const auto& c=request.coordinates;
    const bool lower=c.side==BoundarySide::Lower;
    if(c.axis==BoundaryAxis::X1)face[0]=lower?grid.x1_min:grid.x1_max;
    else face[1]=lower?grid.x2_min:grid.x2_max;
    return MakeBoundaryCoordinates(grid,face,c.axis,c.side,c.time,c.ghost_depth,c.purpose,
        ghost,GridMetrics::GeometrySemantics::AxisymmetricRz);
}

/** State-channel presence only; actual scalar laws stay in the sole resolver. */
inline bool changes_state(const PhysicalBoundaryData& data)
{
    const auto changing=[](const ScalarBoundaryCondition& condition) {
        return condition.kind==ScalarBoundaryKind::Value
            ||condition.kind==ScalarBoundaryKind::NormalGradient;
    };
    bool result=changing(data.temperature);
    for(const auto& condition:data.velocity)result=result||changing(condition);
    for(const auto& condition:data.species)result=result||changing(condition);
    return result;
}

/** Existing face-control schema cannot encode varying kinds or presence.
 * Values may vary physically; the center data alone is transport authority.
 */
inline void require_same_channels(const PhysicalBoundaryData& center,
    const PhysicalBoundaryData& sample)
{
    if(center.hydro.has_value()!=sample.hydro.has_value()
       ||center.temperature.kind!=sample.temperature.kind
       ||center.species.size()!=sample.species.size())
        throw std::invalid_argument("native RZ boundary sample changes center channel kind or presence");
    for(std::size_t axis=0;axis<center.velocity.size();++axis)
        if(center.velocity[axis].kind!=sample.velocity[axis].kind)
            throw std::invalid_argument("native RZ boundary sample changes center channel kind or presence");
    for(std::size_t s=0;s<center.species.size();++s)
        if(center.species[s].kind!=sample.species[s].kind)
            throw std::invalid_argument("native RZ boundary sample changes center channel kind or presence");
}

/** Actual strict EOS/bounds check of a numerical closure's physical point. */
template<class Eos>
inline PrimitiveData physical_primitive(const RzThermodynamics::Cell& closure,double radius,
    std::span<const double> fractions,const arch::state::Bounds& bounds,const Eos& eos,
    FluidVector& point)
{
    if(!closure.valid()||!std::isfinite(radius)||radius<closure.density.lower
       ||radius>closure.density.upper)
        throw std::runtime_error("native RZ boundary has no valid in-cell numerical closure");
    point=RzThermodynamics::base_point(closure,radius);
    const double* xi=fractions.empty()?nullptr:fractions.data();
    if(arch::state::validate_eos(point,xi,static_cast<int>(fractions.size()),bounds,eos)
        !=arch::state::Status::valid)
        throw std::runtime_error("native RZ boundary physical point lies outside the selected EOS or bounds");
    return BoundaryInteriorPrimitive(point,fractions,eos);
}

/** Check actual target V/W and return the unchanged eight Gauss samples.
 * The operation order matches the original callback path: dz, V, W, finite
 * checks, then the shared sample constructor. No face or mean is synthesized.
 */
inline std::array<GridMetrics::Rz::CellAverageSample,8> cell_samples(const CellSupport& target)
{
    const double dz=target.z_upper-target.z_lower;
    const double volume=GridMetrics::Rz::CellVolume(target.r_lower,target.r_upper,dz);
    const double angular=GridMetrics::Rz::AngularMomentumMeasure(target.r_lower,target.r_upper,dz);
    if(!std::isfinite(volume)||!(volume>0.)||!std::isfinite(angular)||!(angular>0.))
        throw std::runtime_error("native RZ target cell V/W measures are not representable");
    return GridMetrics::Rz::CellAverageSamples(target.r_lower,target.r_upper,
        target.z_lower,target.z_upper);
}

/** Integrate one complete read-only point law using the original sample order.
 * rho/mr/mz/E use sum(w_V U), mphi uses sum(w_W rho*vphi), and
 * Xi=sum(w_V*rho*Xi)/sum(w_V*rho) uses the sole scaled ratio implementation.
 * Source and reflected point EOS checks precede this leaf. The result is only
 * provisional finite/rho/simplex data; completed-ghost EOS is owned by Runtime.
 * Conditions are copied verbatim for callbacks, or empty for built-in laws.
 */
inline PhysicalBoundaryEvaluation integrate_samples(
    const std::array<GridMetrics::Rz::CellAverageSample,8>& samples,
    const std::array<FluidVector,8>& points,const std::array<double,8>& weights,
    const std::array<double,8>& densities,const std::vector<double>& fractions,
    int count,const arch::state::Bounds& bounds,const PhysicalBoundaryData& conditions)
{
    constexpr std::size_t sample_count=8;
    const auto conserved=RzCellAverage::conserved_mean(samples,
        [&points](std::size_t k) {return points[k];});
    if(!conserved.valid())throw std::runtime_error("native RZ boundary conserved integration failed");
    PhysicalBoundaryEvaluation result;
    result.conserved=conserved.value;result.conditions=conditions;
    result.mass_fractions.resize(static_cast<std::size_t>(count));
    for(int s=0;s<count;++s) {
        const auto averaged=RzCellAverage::fraction_mean(weights,densities,
            [&fractions,s](std::size_t k) {return fractions[static_cast<std::size_t>(s)*sample_count+k];});
        if(!averaged.valid())throw std::runtime_error("native RZ boundary composition integration failed");
        result.mass_fractions[s]=averaged.value;
    }
    if(RzThermodynamics::provisional_native_state(result.conserved,result.mass_fractions.data(),
        count,1,bounds)!=arch::state::Status::valid)
        throw std::runtime_error("native RZ integrated candidate has invalid native fields or composition");
    return result;
}

} // namespace native_rz_detail

/** Build one read-only positive-r native ghost with the shared point law.
 * Callback order is actual center then the eight existing Gauss samples.
 * The immutable readers borrow the actual seed/completed candidate prefix;
 * stage identity and x1-before-x2 publication remain the manager's authority.
 */
template<class Eos,class Callback,class StateReader,class FractionReader>
PhysicalBoundaryEvaluation EvaluateNativeRzBoundaryCell(const Grid& grid,
    const NativeRzBoundaryRequest& request,const SimConfig& config,const SpeciesManager& species,
    const Eos& eos,const Callback& callback,const StateReader& read,const FractionReader& fraction)
{
    const auto geometry=GridMetrics::make_geometry_view(grid,
        GridMetrics::GeometrySemantics::AxisymmetricRz);
    native_rz_detail::validate_request(grid,request);
    const auto source=native_rz_detail::support(grid,request.source);
    const auto target=native_rz_detail::support(grid,request.destination);
    const arch::state::Bounds bounds{config.numerics.sml_rho,config.numerics.min_eint,
        config.numerics.max_eint};
    if(!arch::state::valid_bounds(bounds)||species.count()<0)
        throw std::invalid_argument("native RZ boundary requires valid numerics bounds and species layout");
    const int count=species.count();
    const int source_index=grid.GetIndex(request.source[0],request.source[1],0);
    const int target_index=grid.GetIndex(request.destination[0],request.destination[1],0);
    std::vector<double> source_x(static_cast<std::size_t>(count)),target_x(static_cast<std::size_t>(count));
    for(int s=0;s<count;++s) {
        source_x[s]=fraction(s,source_index);target_x[s]=fraction(s,target_index);
    }
    const auto inherited=read(target_index);
    if(RzThermodynamics::provisional_native_state(read(source_index),source_x.data(),count,1,bounds)
        !=arch::state::Status::valid)
        throw std::runtime_error("native RZ boundary source has invalid native fields or composition");
    const int source_begin=std::clamp(request.source[0]-1,0,grid.GetTotalX()-3);
    const auto source_closure=RzThermodynamics::make_cell_supported(read,source_index,
        geometry,request.source[0],source_begin,bounds);
    const double center_r=grid.GetCellCenterX(request.destination[0]);
    const double center_z=grid.GetCellCenterY(request.destination[1]);
    const auto source_radius=[&](double r,double z) {
        // Validate both cell fractions even though the radial baseline is
        // independent of axial coordinate; a clamped tangent is not extrapolation.
        (void)native_rz_detail::mapped_coordinate(z,target.z_lower,target.z_upper,
            source.z_lower,source.z_upper,request.coordinates.axis==BoundaryAxis::X2);
        return native_rz_detail::mapped_coordinate(r,target.r_lower,target.r_upper,
            source.r_lower,source.r_upper,request.coordinates.axis==BoundaryAxis::X1);
    };
    FluidVector source_point;
    const auto source_primitive=native_rz_detail::physical_primitive(source_closure,
        source_radius(center_r,center_z),source_x,bounds,eos,source_point);
    const auto center_coordinates=native_rz_detail::point_coordinates(grid,request,center_r,center_z);
    const PhysicalBoundaryContext center_context(center_coordinates,config,species,source_primitive);
    const PhysicalBoundaryData center_data=callback(center_context);
    ValidatePhysicalBoundaryData(center_data,config,count);
    const bool target_base=center_coordinates.purpose==BoundaryPurpose::Diffusion
        &&!center_data.hydro&&native_rz_detail::changes_state(center_data);
    RzThermodynamics::Cell target_closure;
    PrimitiveData target_primitive;
    if(target_base) {
        if(RzThermodynamics::provisional_native_state(inherited,target_x.data(),count,1,bounds)
            !=arch::state::Status::valid)
            throw std::runtime_error("native RZ inherited target has invalid native fields or composition");
        const int target_begin=std::clamp(request.destination[0]-1,0,grid.GetTotalX()-3);
        target_closure=RzThermodynamics::make_cell_supported(read,target_index,
            geometry,request.destination[0],target_begin,bounds);
        FluidVector target_point;
        target_primitive=native_rz_detail::physical_primitive(target_closure,center_r,
            target_x,bounds,eos,target_point);
    }
    const auto center=ResolvePhysicalBoundaryData(center_coordinates,config,eos,center_data,
        source_point,source_x,source_primitive,&inherited,target_x,
        target_base?&target_primitive:nullptr);
    if(!center_data.hydro&&!native_rz_detail::changes_state(center_data)) {
        if(RzThermodynamics::provisional_native_state(center.conserved,center.mass_fractions.data(),
            count,1,bounds)!=arch::state::Status::valid)
            throw std::runtime_error("native RZ inherited candidate has invalid native fields or composition");
        return center; // Exact original U/X and face-center conditions; no quadrature.
    }

    const auto samples=native_rz_detail::cell_samples(target);
    constexpr std::size_t sample_count=8;
    std::array<FluidVector,sample_count> points{};
    std::array<double,sample_count> weights{},densities{};
    std::vector<double> fractions(static_cast<std::size_t>(count)*sample_count);
    for(std::size_t k=0;k<sample_count;++k) {
        const auto& q=samples[k];
        FluidVector interior_point;
        const auto interior_primitive=native_rz_detail::physical_primitive(source_closure,
            source_radius(q.radius,q.axial),source_x,bounds,eos,interior_point);
        const auto coordinates=native_rz_detail::point_coordinates(grid,request,q.radius,q.axial);
        const PhysicalBoundaryContext context(coordinates,config,species,interior_primitive);
        const PhysicalBoundaryData data=callback(context);
        ValidatePhysicalBoundaryData(data,config,count);
        native_rz_detail::require_same_channels(center_data,data);
        PrimitiveData base;
        if(target_base) {
            FluidVector target_point;
            base=native_rz_detail::physical_primitive(target_closure,q.radius,
                target_x,bounds,eos,target_point);
        }
        const auto resolved=ResolvePhysicalBoundaryData(coordinates,config,eos,data,interior_point,
            source_x,interior_primitive,&inherited,target_x,target_base?&base:nullptr);
        if(resolved.mass_fractions.size()!=static_cast<std::size_t>(count)
           ||arch::state::validate_eos(resolved.conserved,resolved.mass_fractions.data(),count,bounds,eos)
                !=arch::state::Status::valid)
            throw std::runtime_error("native RZ resolved sample lies outside the selected EOS or bounds");
        points[k]=resolved.conserved;weights[k]=q.volume_weight;densities[k]=points[k].rho;
        for(int s=0;s<count;++s)
            fractions[static_cast<std::size_t>(s)*sample_count+k]=resolved.mass_fractions[s];
    }
    return native_rz_detail::integrate_samples(samples,points,weights,densities,
        fractions,count,bounds,center.conditions);
}

/** Build one callback-free physical reflecting ghost from true source points.
 *
 * Workflow:
 * 1. Require actual source/target mirror indices and positive physical support.
 * 2. Validate all three real density-support states and their composition, then
 *    bind the existing supported density/inertia closure without raw recovery.
 * 3. Map each of eight target Gauss points into its actual source support and
 *    require that source physical primitive through the real selected EOS.
 * 4. Apply only BoundaryPlan's normal momentum sign; retain point rho/E,
 *    tangential momenta and the complete Xi. Require the mirrored point EOS.
 * 5. Reuse exactly the callback law's V/W and density-weighted Xi integration;
 *    return unpublished provisional data with no user or transport controls.
 *
 * At a nonzero radial wall R and rigid source rotation, the physical law is
 * vphi_g(r)=Omega*(2R-r), not a raw native mphi/E sign copy. Projection stores
 * <rho*vphi>_W and <E>_V; a later constant-Omega closure can contain unresolved
 * kinetic variance and need not recover the source e0 exactly. No E is added.
 * The trusted caller owns actual physical-face/stage authority. Negative or
 * crossing-zero targets are rejected; regular-axis signed copies stay outside
 * this positive-cell law. ENUC and publication remain outside this leaf.
 */
template<class Eos,class StateReader,class FractionReader>
PhysicalBoundaryEvaluation EvaluateNativeRzReflectingCell(const Grid& grid,
    const NativeRzBoundaryRequest& request,const SimConfig& config,const SpeciesManager& species,
    const Eos& eos,const StateReader& read,const FractionReader& fraction)
{
    const auto geometry=GridMetrics::make_geometry_view(grid,
        GridMetrics::GeometrySemantics::AxisymmetricRz);
    native_rz_detail::validate_request(grid,request);
    const auto source=native_rz_detail::support(grid,request.source);
    const auto target=native_rz_detail::support(grid,request.destination);
    const arch::state::Bounds bounds{config.numerics.sml_rho,config.numerics.min_eint,
        config.numerics.max_eint};
    if(!arch::state::valid_bounds(bounds)||species.count()<0)
        throw std::invalid_argument("native RZ reflecting boundary requires valid numerics bounds and species layout");
    const int count=species.count();
    const int source_index=grid.GetIndex(request.source[0],request.source[1],0);
    const int source_begin=std::clamp(request.source[0]-1,0,grid.GetTotalX()-3);
    std::vector<double> source_x(static_cast<std::size_t>(count)),support_x(static_cast<std::size_t>(count));
    for(int s=0;s<count;++s)source_x[s]=fraction(s,source_index);
    // Real signed axis-reflected density cells may supply the positive donor's
    // three-cell stencil. They are native states, never negative-r EOS points.
    for(int i=source_begin;i<source_begin+3;++i) {
        const int index=grid.GetIndex(i,request.source[1],0);
        for(int s=0;s<count;++s)support_x[s]=fraction(s,index);
        if(RzThermodynamics::provisional_native_state(read(index),support_x.data(),count,1,bounds)
            !=arch::state::Status::valid)
            throw std::runtime_error("native RZ reflecting source support has invalid native fields or composition");
    }
    const auto source_closure=RzThermodynamics::make_cell_supported(read,source_index,
        geometry,request.source[0],source_begin,bounds);
    if(!source_closure.valid())
        throw std::runtime_error("native RZ reflecting boundary has no admissible supported source closure");
    const auto samples=native_rz_detail::cell_samples(target);
    constexpr std::size_t sample_count=8;
    std::array<FluidVector,sample_count> points{};
    std::array<double,sample_count> weights{},densities{};
    std::vector<double> fractions(static_cast<std::size_t>(count)*sample_count);
    const auto axis=request.coordinates.axis;
    for(std::size_t k=0;k<sample_count;++k) {
        const auto& q=samples[k];
        // The radial baseline is constant in z within its actual donor cell;
        // still authenticate the real axial fraction and normal reflection.
        (void)native_rz_detail::mapped_coordinate(q.axial,target.z_lower,target.z_upper,
            source.z_lower,source.z_upper,axis==BoundaryAxis::X2);
        const double radius=native_rz_detail::mapped_coordinate(q.radius,
            target.r_lower,target.r_upper,source.r_lower,source.r_upper,axis==BoundaryAxis::X1);
        FluidVector reflected;
        (void)native_rz_detail::physical_primitive(source_closure,radius,
            source_x,bounds,eos,reflected);
        if(axis==BoundaryAxis::X1)
            reflected.mom_u*=reflection_sign(axis,BoundaryType::Reflecting,BoundaryFieldClass::MomentumX);
        else
            reflected.mom_v*=reflection_sign(axis,BoundaryType::Reflecting,BoundaryFieldClass::MomentumY);
        if(arch::state::validate_eos(reflected,source_x.data(),count,bounds,eos)
            !=arch::state::Status::valid)
            throw std::runtime_error("native RZ reflected physical sample lies outside the selected EOS or bounds");
        points[k]=reflected;weights[k]=q.volume_weight;densities[k]=points[k].rho;
        // Keep fractions, without a premature rho*Xi product that could lose a
        // positive trace. The shared scaled density-weighted ratio owns Xi.
        for(int s=0;s<count;++s)
            fractions[static_cast<std::size_t>(s)*sample_count+k]=source_x[s];
    }
    return native_rz_detail::integrate_samples(samples,points,weights,densities,
        fractions,count,bounds,PhysicalBoundaryData{});
}

} // namespace arch::boundary
