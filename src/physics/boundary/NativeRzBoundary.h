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
#include "physics/boundary/NativeRzBoundaryMath.h"
#include "physics/boundary/PhysicalBoundary.h"
#include "physics/species/Species.h"

namespace arch::boundary {

/** Actual logical donor/target indices and the frozen physical center request. */
struct NativeRzBoundaryRequest {
    std::array<int,2> source{},destination{};
    BoundaryCoordinates coordinates;
};

namespace native_rz_detail {
/** Bind the actual logical extents and active intervals to the borrowed view. */
inline native_rz_math::Context context(const Grid& grid,const GridMetrics::GeometryView& geometry)
{
    return {geometry,grid.GetTotalX(),grid.GetTotalY(),grid.Is(),grid.Ie(),grid.Js(),grid.Je()};
}

/** Lower only numerical request values; callback/stage coordinates stay Host-owned. */
inline native_rz_math::Request numerical_request(const NativeRzBoundaryRequest& request)
{
    return {request.source,request.destination,request.coordinates.axis,request.coordinates.side,
        request.coordinates.ghost_depth,request.coordinates.purpose};
}

/** Preserve input-error versus physical/integration-error exception categories. */
inline void require_success(native_rz_math::Status status)
{
    using Status=native_rz_math::Status;
    switch(status) {
    case Status::valid:return;
    case Status::invalid_request:
        throw std::invalid_argument("native RZ boundary requires an actual two-dimensional physical ghost mirror request");
    case Status::invalid_cell:
        throw std::invalid_argument("native RZ boundary requires real logical source and target cells");
    case Status::invalid_support:
        throw std::invalid_argument("native RZ physical callback requires a positive radial cell with finite ordered bounds");
    case Status::invalid_source_layout:
        throw std::invalid_argument("native RZ boundary source requires a real radial layout/index");
    case Status::insufficient_active_support:
        throw std::invalid_argument("native RZ boundary active source requires three real active density cells");
    case Status::invalid_bounds_or_species:
        throw std::invalid_argument("native RZ reflecting boundary requires valid numerics bounds and species layout");
    case Status::invalid_workspace:
        throw std::invalid_argument("native RZ reflecting boundary requires its complete caller-owned species workspace");
    case Status::invalid_source_state:
        throw std::runtime_error("native RZ reflecting source support has invalid native fields or composition");
    case Status::invalid_source_closure:
        throw std::runtime_error("native RZ reflecting boundary has no admissible supported source closure");
    case Status::invalid_point_closure:
        throw std::runtime_error("native RZ boundary has no valid in-cell numerical closure");
    case Status::invalid_point_eos:
        throw std::runtime_error("native RZ boundary physical point lies outside the selected EOS or bounds");
    case Status::invalid_point_thermal:
        throw std::runtime_error("interior boundary state has no resolvable thermal energy");
    case Status::invalid_point_composition:
        throw std::invalid_argument("interior boundary composition must supply finite non-negative mass fractions summing to one");
    case Status::invalid_point_temperature:
        throw std::runtime_error("interior boundary state lies outside the selected EOS temperature domain");
    case Status::invalid_point_pressure:
        throw std::runtime_error("interior boundary state lies outside the selected EOS pressure domain");
    case Status::invalid_mapped_fraction:
        throw std::runtime_error("native RZ boundary target sample lies outside its actual cell");
    case Status::invalid_mapped_point:
        throw std::runtime_error("native RZ boundary mapped point lies outside its actual donor cell");
    case Status::invalid_target_measures:
        throw std::runtime_error("native RZ target cell V/W measures are not representable");
    case Status::invalid_reflected_eos:
        throw std::runtime_error("native RZ reflected physical sample lies outside the selected EOS or bounds");
    case Status::conserved_integration_failed:
        throw std::runtime_error("native RZ boundary conserved integration failed");
    case Status::fraction_integration_failed:
        throw std::runtime_error("native RZ boundary composition integration failed");
    case Status::invalid_candidate:
        throw std::runtime_error("native RZ integrated candidate has invalid native fields or composition");
    }
    throw std::runtime_error("native RZ boundary returned an unknown numerical failure status");
}

using CellSupport=native_rz_math::CellSupport;

/** Borrow the sole positive logical-cell support leaf and translate its status. */
inline CellSupport support(const native_rz_math::Context& context,const std::array<int,2>& cell)
{
    CellSupport result{};
    require_success(native_rz_math::support(context,cell,result));return result;
}

/** Authenticate the original Host request before callback or donor reads. */
inline void validate_request(const native_rz_math::Context& context,
    const NativeRzBoundaryRequest& request)
{
    if(request.coordinates.dimension!=2)
        throw std::invalid_argument("native RZ boundary requires an actual two-dimensional physical ghost request");
    require_success(native_rz_math::validate_request(context,numerical_request(request)));
}

/** Reuse the sole active/completed-corner three-density support selection. */
inline int source_support_begin(const native_rz_math::Context& context,int source_i)
{
    int begin=0;require_success(native_rz_math::source_support_begin(context,source_i,begin));return begin;
}

/** Map through the same normal/tangent fractions as the device numerical leaf. */
inline double mapped_coordinate(double target,double target_lower,double target_upper,
    double source_lower,double source_upper,bool reflect)
{
    double point=0.;
    require_success(native_rz_math::mapped_coordinate(target,target_lower,target_upper,
        source_lower,source_upper,reflect,point));return point;
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

/** Construct the Host snapshot from the sole checked physical-point leaf.
 * The scalar owner preserves validate_eos and the original extra T/P queries;
 * only copied Xi ownership and PrimitiveData's vector remain Host-specific.
 */
template<class Eos>
inline PrimitiveData physical_primitive(const RzThermodynamics::Cell& closure,double radius,
    std::span<const double> fractions,const arch::state::Bounds& bounds,const Eos& eos,
    FluidVector& point)
{
    native_rz_math::Primitive scalar;
    require_success(native_rz_math::physical_point(closure,radius,
        fractions.empty()?nullptr:fractions.data(),static_cast<int>(fractions.size()),bounds,eos,point,scalar));
    PrimitiveData result;
    result.rho=scalar.rho;result.u=scalar.u;result.v=scalar.v;result.w=scalar.w;
    result.temperature=scalar.temperature;result.p=scalar.pressure;result.has_temperature=true;
    result.mass_fractions.assign(fractions.begin(),fractions.end());return result;
}

/** Borrow the original target dz/V/W precheck and unchanged eight Gauss points. */
inline std::array<GridMetrics::Rz::CellAverageSample,8> cell_samples(const CellSupport& target)
{
    std::array<GridMetrics::Rz::CellAverageSample,8> result{};
    require_success(native_rz_math::cell_samples(target,result));return result;
}

/** Reuse the sole V/W and scaled density-weighted Xi integral for callbacks.
 * Point EOS checks precede integration; conditions are copied verbatim and
 * completed-ghost EOS/publication remain the Runtime owner's responsibility.
 */
inline PhysicalBoundaryEvaluation integrate_samples(
    const std::array<GridMetrics::Rz::CellAverageSample,8>& samples,
    const std::array<FluidVector,8>& points,const std::array<double,8>& weights,
    const std::array<double,8>& densities,const std::vector<double>& fractions,
    int count,const arch::state::Bounds& bounds,const PhysicalBoundaryData& conditions)
{
    PhysicalBoundaryEvaluation result;
    result.mass_fractions.resize(static_cast<std::size_t>(count));
    const auto integrated=native_rz_math::integrate_samples(samples,points,weights,densities,
        fractions.empty()?nullptr:fractions.data(),count,bounds,
        result.mass_fractions.empty()?nullptr:result.mass_fractions.data());
    require_success(integrated.status);
    result.conserved=integrated.conserved;result.conditions=conditions;return result;
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
    const auto numerical_context=native_rz_detail::context(grid,geometry);
    native_rz_detail::validate_request(numerical_context,request);
    const auto source=native_rz_detail::support(numerical_context,request.source);
    const auto target=native_rz_detail::support(numerical_context,request.destination);
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
    const int source_begin=native_rz_detail::source_support_begin(numerical_context,request.source[0]);
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
    const auto numerical_context=native_rz_detail::context(grid,geometry);
    native_rz_detail::validate_request(numerical_context,request);
    const arch::state::Bounds bounds{config.numerics.sml_rho,config.numerics.min_eint,
        config.numerics.max_eint};
    const int count=species.count();
    std::size_t workspace_size=0;
    if(!arch::state::valid_bounds(bounds)||!native_rz_math::workspace_extent(count,workspace_size))
        throw std::invalid_argument("native RZ reflecting boundary requires valid numerics bounds and species layout");
    std::vector<double> workspace(workspace_size);
    PhysicalBoundaryEvaluation result;
    result.mass_fractions.resize(static_cast<std::size_t>(count));
    const native_rz_math::Workspace borrowed{workspace.empty()?nullptr:workspace.data(),workspace.size(),
        result.mass_fractions.empty()?nullptr:result.mass_fractions.data(),result.mass_fractions.size()};
    const auto reflected=native_rz_math::reflect_cell(numerical_context,
        native_rz_detail::numerical_request(request),bounds,count,eos,read,fraction,borrowed);
    native_rz_detail::require_success(reflected.status);
    result.conserved=reflected.conserved;return result;
}

} // namespace arch::boundary
