/**
 * @file RzNativeClosure.h
 * @brief Shared numerical thermodynamic closure of native full-ring RZ means.
 *
 * Workflow:
 * 1. Borrow the density polynomial/moments of the same stage and ghost stencil.
 * 2. Map m_phi=J/W to an effective ordinary momentum with the same rotational
 *    kinetic mean, then use the existing strict energy/bounds validation.
 * 3. Expose the mean EOS input and a conservative physical baseline at radius r.
 * 4. Precheck raw native means without treating J/W as a point momentum.
 * 5. After actual whole-domain BC/exchange, validate the mean and the same
 *    physical baseline at both faces and the four shared radial Gauss nodes.
 * 6. Completed-patch callers traverse logical ghosts with an explicit real
 *    three-column support; storage padding is never a logical boundary.
 *
 * I_* is the inertia of the explicit numerical density reconstruction, not a
 * certificate of an unknown subcell physical field. No new evolved field, EOS,
 * floor, heating term or backend-specific formula is introduced. The caller
 * owns slot/version/ghost/topology identity and the final physical EOS checks.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/ArchPortability.h"
#include "data/FluidState.h"
#include "grid/GridMetrics.h"
#include "numerics/reconstruction/RzDensityMoments.h"
#include "numerics/state/StateAdmissibility.h"

namespace RzThermodynamics {

/** Stack-local native closure; effective_mean is an EOS input, not evolved U. */
struct Cell {
    RzDensity::Cell density;
    FluidVector effective_mean{};
    double radial_velocity=0.,axial_velocity=0.,omega=0.,internal=0.;
    arch::state::Status status=arch::state::Status::invalid_thermodynamics;
    ARCH_INLINE bool valid() const {return status==arch::state::Status::valid;}
};

/** Convert one native mean using the explicit density reconstruction inertia.
 * kappa=rho_V*W^2/(V*I_*). Factoring out h and radius gives
 * kappa=rho_V*weighted_two^2/(density_scale*weighted_V*abs(weighted_three)),
 * weighted_V=abs(r_left/R)+abs(r_right/R). This works for reflected negative
 * ghosts as well: inertia in the kinetic energy is positive while omega retains
 * the signed density capacity/parity used by the physical point profile.
 */
ARCH_INLINE Cell from_density(const FluidVector& native,const RzDensity::Cell& density,
    const arch::state::Bounds& bounds={})
{
    Cell result{};result.density=density;
    if(!RzDensity::detail::cell_valid(density))return result;
    if(!std::isfinite(native.rho)||!std::isfinite(native.mom_u)
       ||!std::isfinite(native.mom_v)||!std::isfinite(native.mom_w)
       ||!std::isfinite(native.eng)) {
        result.status=arch::state::Status::nonfinite;return result;
    }
    if(!(native.rho>0.)) {
        result.status=arch::state::Status::nonpositive_density;return result;
    }
    if(!arch::state::valid_bounds(bounds))return result;
    const double volume_weight=std::abs(density.lower/density.radius_scale)
        +std::abs(density.upper/density.radius_scale);
    const double numerator[]{native.rho,density.weighted_two,density.weighted_two};
    const double denominator[]{density.density_scale,volume_weight,
        std::abs(density.weighted_three)};
    const double kappa=RzDensity::detail::scaled_value(numerator,3,denominator,3);
    if(!std::isfinite(kappa)||!(kappa>0.))return result;
    result.effective_mean=native;
    const double momentum[]{native.mom_w,std::sqrt(kappa)};
    result.effective_mean.mom_w=RzDensity::detail::scaled_value(momentum,2);
    result.omega=RzDensity::angular_velocity(native.mom_w,density);
    if(!std::isfinite(result.omega)) {
        result.status=arch::state::Status::nonfinite;return result;
    }
    result.status=arch::state::validate(result.effective_mean,nullptr,0,1,
        bounds.density,bounds.internal_min,bounds.internal_max);
    if(!result.valid())return result;
    const auto kinematics=arch::state::recover(result.effective_mean);
    result.radial_velocity=kinematics.u;result.axial_velocity=kinematics.v;
    result.internal=kinematics.internal;
    return result;
}

/** Bind the same three native density means and real signed cell geometry. */
template<class StateReader>
ARCH_INLINE Cell make_cell(const StateReader& read,int index,
    const GridMetrics::GeometryView& grid,int i,const arch::state::Bounds& bounds={})
{
    return from_density(read(index),RzDensity::density_cell(read,index,grid,i),bounds);
}

/** Bind an explicit real three-cell radial support containing this target.
 * The caller supplies its logical support begin; no halo is guessed or read
 * beyond that support. Signed reflected ghosts reuse the same density moments,
 * capacity and angular parity as the centered leaf. No floor or EOS is added.
 */
template<class StateReader>
ARCH_INLINE Cell make_cell_supported(const StateReader& read,int index,
    const GridMetrics::GeometryView& grid,int i,int support_begin,
    const arch::state::Bounds& bounds={})
{
    return from_density(read(index),
        RzDensity::density_cell_supported(read,index,grid,i,support_begin),bounds);
}

/** Physical conservative baseline whose mathematical V/W means are native U.
 * m_r=rho*u_r, m_z=rho*u_z, m_phi=rho*omega*r;
 * E=rho*e0 + (m_r*u_r+m_z*u_z+m_phi*u_phi)/2.
 * Caller checks the resulting point with the actual EOS/physical bounds.
 */
ARCH_INLINE FluidVector base_point(const Cell& cell,double radius)
{
    if(!cell.valid()||!std::isfinite(radius)) {
        const double bad=std::numeric_limits<double>::quiet_NaN();
        return {bad,bad,bad,bad,bad};
    }
    const double rho=cell.density.density.at((radius-cell.density.origin)/cell.density.spacing);
    const double velocity=cell.omega*radius;
    const double mr=rho*cell.radial_velocity,mz=rho*cell.axial_velocity,mphi=rho*velocity;
    const double energy=rho*cell.internal+(.5*mr)*cell.radial_velocity
        +(.5*mz)*cell.axial_velocity+(.5*mphi)*velocity;
    return {rho,mr,mz,mphi,energy};
}

/** Physical derivative of exactly the same baseline, including rotational work. */
ARCH_INLINE FluidVector base_derivative(const Cell& cell,double radius)
{
    if(!cell.valid()||!std::isfinite(radius))return base_point(cell,arch::state::invalid());
    const double t=(radius-cell.density.origin)/cell.density.spacing;
    const auto& polynomial=cell.density.density;
    const double rho=polynomial.at(t);
    // Keep 2*t together: 2*c2 can overflow at t=0 even when p'(0)=c1 is finite.
    const double gradient=(polynomial.linear+polynomial.quadratic*(2.*t))/cell.density.spacing;
    const double velocity=cell.omega*radius;
    const double angular=gradient*velocity+rho*cell.omega;
    const double mr=gradient*cell.radial_velocity,mz=gradient*cell.axial_velocity;
    const double energy=gradient*cell.internal+(.5*mr)*cell.radial_velocity
        +(.5*mz)*cell.axial_velocity+(.5*angular)*velocity
        +(.5*rho*velocity)*cell.omega;
    return {gradient,mr,mz,angular,energy};
}

/** Finite/rho/simplex precheck of native U, without energy recovery.
 * This is provisional only: J/W and E/V use different measures. Bounds are
 * checked as configuration, but raw E is never compared with a Cartesian
 * kinetic energy. No repair, floor, normalization or publication occurs.
 */
ARCH_INLINE arch::state::Status provisional_native_state(const FluidVector& native,
    const double* fractions,int species,int stride,const arch::state::Bounds& bounds={})
{
    if(!arch::state::valid_bounds(bounds))
        return arch::state::Status::invalid_thermodynamics;
    if(!std::isfinite(native.rho)||!std::isfinite(native.mom_u)
        ||!std::isfinite(native.mom_v)||!std::isfinite(native.mom_w)
        ||!std::isfinite(native.eng))return arch::state::Status::nonfinite;
    if(!(native.rho>0.))return arch::state::Status::nonpositive_density;
    if(native.rho<bounds.density)return arch::state::Status::invalid_thermodynamics;
    return arch::state::validate_composition(fractions,species,stride);
}

/** Apply the actual EOS to an already-constructed numerical mean closure.
 * effective_mean has the same rotational kinetic mean J^2/(2 I_* V), so
 * ordinary strict recovery is now appropriate. Cell construction has checked
 * the caller's bounds; composition and EOS use contiguous Xi. A mean PASS alone
 * does not establish physical-point acceptance or any ghost/topology epoch.
 * EOS exceptions propagate to the caller's transaction owner.
 */
template<class Eos>
ARCH_INLINE arch::state::Status validate_mean_eos(const Cell& cell,
    const double* contiguous_fractions,int species,const Eos& eos)
{
    if(!cell.valid())return cell.status;
    return arch::state::validate_eos(cell.effective_mean,contiguous_fractions,
        species,{},eos);
}

inline constexpr int physical_node_count=6;

/** Radius on this real accepted cell: both faces, then four Gauss nodes.
 * Use actual lower/upper bounds, rather than recomputing origin +/- h/2,
 * because those can have different FP64 endpoints on a generated grid.
 */
ARCH_INLINE double physical_node_radius(const Cell& cell,int node)
{
    if(node<0||node>=physical_node_count)return arch::state::invalid();
    const double lower=cell.density.lower,upper=cell.density.upper;
    if(node==0)return lower;
    if(node==1)return upper;
    const double half=.5*(upper-lower),midpoint=lower+half;
    return midpoint+half*RzReconstruction::quadrature_node(node-2);
}

/** Validate one read-only Host patch after its real density ghosts exist.
 * Workflow: check actual chart/layout; precheck each native mean; construct
 * rho_* and I_* from its real radial neighbors; evaluate the actual EOS for
 * the effective mean and its six baseline physical points with the same Xi.
 * No arrays or means are changed. This certifies the numerical baseline at
 * these points, not an unknown subcell field or an already-evaluated high ray.
 * The Runtime caller owns exact slot/version/topology, completed BC/exchange,
 * and rollback before publication. Device traversal reuses the scalar leaves
 * rather than materializing an entire device patch on the Host.
 */
namespace detail {
/** Shared read-only Host region gate: preserve native precheck, actual mean EOS,
 * both true faces and four radial Gauss-node EOS checks with the original
 * diagnostics/bounds. Region and closure selection remain explicit callers.
 */
template<class Eos,class CellClosure>
inline void validate_patch_eos_region(const FluidState& state,const Grid& grid,int species,
    const arch::state::Bounds& bounds,const Eos& eos,int i_begin,int i_end,
    int j_begin,int j_end,const CellClosure& closure)
{
    const auto view=GridMetrics::make_geometry_view(grid,
        GridMetrics::GeometrySemantics::AxisymmetricRz);
    const int extent=grid.GetTotalSize();
    if(!arch::state::valid_bounds(bounds)||grid.ng<1||extent<=0
        ||species<0||state.GetNumSpecies()!=species||state.block_total_size_!=extent
        ||state.rho.size()!=static_cast<std::size_t>(extent)
        ||state.mom_u.size()!=state.rho.size()||state.mom_v.size()!=state.rho.size()
        ||state.mom_w.size()!=state.rho.size()||state.eng.size()!=state.rho.size()
        ||state.mass_fractions.size()!=static_cast<std::size_t>(species)*extent)
        throw std::invalid_argument("RZ EOS acceptance requires the actual complete patch layout and density ghosts");
    std::vector<double> fractions(static_cast<std::size_t>(species));
    const auto read=[&](int index){return state.get(index);};
    for(int j=j_begin;j<j_end;++j)for(int i=i_begin;i<i_end;++i) {
        const int index=grid.GetIndex(i,j,0);
        for(int s=0;s<species;++s)fractions[s]=state.X(s,index);
        const auto native=read(index);
        const auto preliminary=provisional_native_state(native,fractions.data(),species,1,bounds);
        if(preliminary!=arch::state::Status::valid)
            throw std::runtime_error("RZ native provisional state rejected at cell "+std::to_string(index));
        const auto cell=closure(read,index,view,i,bounds);
        if(validate_mean_eos(cell,fractions.data(),species,eos)!=arch::state::Status::valid)
            throw std::runtime_error("RZ native closure/EOS rejected at cell "+std::to_string(index));
        for(int node=0;node<physical_node_count;++node) {
            const auto point=base_point(cell,physical_node_radius(cell,node));
            if(arch::state::validate_eos(point,fractions.data(),species,bounds,eos)
                !=arch::state::Status::valid)
                throw std::runtime_error("RZ physical baseline/EOS rejected at cell "+std::to_string(index)
                    +", node "+std::to_string(node));
        }
    }
}
} // namespace detail

/** Preserve the original active-interior centered-neighbour EOS acceptance.
 * Existing callers keep their active extent, arithmetic path and diagnostics.
 */
template<class Eos>
inline void validate_patch_eos(const FluidState& state,const Grid& grid,int species,
    const arch::state::Bounds& bounds,const Eos& eos)
{
    detail::validate_patch_eos_region(state,grid,species,bounds,eos,
        grid.Is(),grid.Ie(),grid.Js(),grid.Je(),
        [](const auto& read,int index,const auto& view,int i,const auto& limits) {
            return make_cell(read,index,view,i,limits);
        });
}

/** Validate all completed logical Host cells, including signed axis ghosts.
 * Workflow: require the actual logical layout; select three existing same-row
 * columns with begin=clamp(i-1,0,nx-3); evaluate the same native/mean/physical
 * EOS body before the caller publishes any ghost witness. The x padding in
 * stride_y is storage only, never a logical cell or available density support.
 * A cell straddling the axis is rejected by the shared density support leaf;
 * a reflected negative annulus keeps its signed capacity/omega mathematics.
 * Call only after genuine whole-domain BC/exchange has completed. Constructed
 * test ghosts do not establish BC, AMR transfer or Runtime qualification.
 */
template<class Eos>
inline void validate_completed_patch_eos(const FluidState& state,const Grid& grid,
    int species,const arch::state::Bounds& bounds,const Eos& eos)
{
    const int nx=grid.GetTotalX(),ny=grid.GetTotalY();
    if(nx<3||ny<1||grid.stride_y<nx||grid.stride_z<=0
        ||ny>grid.stride_z/grid.stride_y||grid.GetTotalZ()!=1
        ||grid.GetIndex(nx-1,ny-1,0)>=grid.GetTotalSize())
        throw std::invalid_argument("RZ EOS acceptance requires the actual complete patch layout and density ghosts");
    detail::validate_patch_eos_region(state,grid,species,bounds,eos,0,nx,0,ny,
        [nx](const auto& read,int index,const auto& view,int i,const auto& limits) {
            return make_cell_supported(read,index,view,i,std::clamp(i-1,0,nx-3),limits);
        });
}

} // namespace RzThermodynamics
