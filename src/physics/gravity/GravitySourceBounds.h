/**
 * @file GravitySourceBounds.h
 * @brief Companion error ledgers for the actual Poisson density source.
 *
 * Workflow:
 * 1. Require actual all-periodic boundary topology.
 * 2. Compare the final periodic source with rho minus its normalized mean.
 * 3. Enclose shared CGS G, mathematical pi and the same supplied FP64 rho.
 * 4. Bound stored-weight RMS without altering the produced source.
 *
 * A=-Laplacian: b=-4*pi*G*(rho-mean(rho)) for this periodic companion.
 * Actual source construction and field existence remain with the solver.
 * These are error companions, not a second producer or a field capability.
 */
#pragma once
#include <cmath>
#include <limits>
#include <span>
#include <vector>
#include "physics/constant/PhysicalConstants.h"
#include "numerics/elliptic/CompositePoisson.h"

namespace Physical::Gravity {
enum class GravitySourceBoundStatus {
    Bounded,InvalidInput,UnsupportedPeriodic,UnsupportedNonperiodic,CollapsedToZero,Overflow
};
struct GravitySourceBounds {
    GravitySourceBoundStatus status=GravitySourceBoundStatus::InvalidInput;
    std::vector<double> lower,upper,cell_bounds;
    // Norm is conditional on the stored weights, not ideal geometry.
    double norm_upper=std::numeric_limits<double>::infinity();
};
namespace gravity_source_detail {
struct Range {double lo=0.,hi=0.;};
inline bool finite(Range x) {return std::isfinite(x.lo)&&std::isfinite(x.hi)&&x.lo<=x.hi;}
inline Range negate(Range x){return {-x.hi,-x.lo};}
inline Range add(Range a,Range b) {
    if(a.lo==0.&&a.hi==0.)return b;
    if(b.lo==0.&&b.hi==0.)return a;
    if(a.lo==a.hi&&b.lo==b.hi&&a.lo==-b.lo)return {};
    return {std::nextafter(a.lo+b.lo,-std::numeric_limits<double>::infinity()),
        std::nextafter(a.hi+b.hi,std::numeric_limits<double>::infinity())};
}
inline Range product(Range a,Range b) {
    if((a.lo==0.&&a.hi==0.)||(b.lo==0.&&b.hi==0.))return {};
    const double v[]{a.lo*b.lo,a.lo*b.hi,a.hi*b.lo,a.hi*b.hi};
    double lo=v[0],hi=v[0];
    for(double x:v){lo=std::min(lo,x);hi=std::max(hi,x);}
    return {std::nextafter(lo,-std::numeric_limits<double>::infinity()),
        std::nextafter(hi,std::numeric_limits<double>::infinity())};
}
}
/** Compare actual final periodic source with -4*pi*G*(rho-normalized_mean(rho)).
 * The normalized mean uses the stored weights, NOT ideal geometry. Capture
 * all actual mean/subtraction/multiplication/final projection errors together.
 * Centering is an exact algebraic reference transformation, never a replacement
 * for the production source algorithm or a new density/physics definition.
 */
inline GravitySourceBounds bound_periodic_gravity_source(
    const arch::elliptic::CompositePoisson& op,std::span<const double> density,
    std::span<const double> computed_source) {
    using namespace gravity_source_detail;
    GravitySourceBounds result{};
    if(!op.periodic_boundary()) {
        result.status=GravitySourceBoundStatus::UnsupportedNonperiodic;return result;
    }
    if(density.empty()||density.size()!=static_cast<std::size_t>(op.size())
        ||computed_source.size()!=density.size())return result;
    const auto& weights=op.norm_weights();
    for(std::size_t i=0;i<density.size();++i)
        if(!std::isfinite(density[i])||density[i]<0.||!std::isfinite(computed_source[i])
            ||!std::isfinite(weights[i])||weights[i]<=0.)return result;
    // Exact algebra: rho_i-mean(rho)=(rho_i-a)-mean(rho-a).
    // Avoid introducing background-density cancellation into the reference.
    const double anchor=density[0];
    Range weight_sum{},centered_sum{};
    std::vector<Range> centered;centered.reserve(density.size());
    for(std::size_t i=0;i<density.size();++i) {
        const auto delta=add({density[i],density[i]},{-anchor,-anchor});
        centered.push_back(delta);
        weight_sum=add(weight_sum,{weights[i],weights[i]});
        centered_sum=add(centered_sum,product({weights[i],weights[i]},delta));
    }
    if(!finite(weight_sum)||weight_sum.lo<=0.||!finite(centered_sum)) {
        result.status=GravitySourceBoundStatus::Overflow;return result;
    }
    const Range inverse{std::nextafter(1./weight_sum.hi,0.),
        std::nextafter(1./weight_sum.lo,std::numeric_limits<double>::infinity())};
    const auto mean=product(centered_sum,inverse);
    const double pi=arch::constants::math::pi;
    const double G=arch::constants::gravity::cgs::gravitational_constant;
    const auto factor=negate(product(product({4.,4.},
        {std::nextafter(pi,0.),std::nextafter(pi,std::numeric_limits<double>::infinity())}),{G,G}));
    result.lower.resize(density.size());result.upper.resize(density.size());
    result.cell_bounds.resize(density.size());
    for(std::size_t i=0;i<density.size();++i) {
        const auto exact=product(factor,add(centered[i],negate(mean)));
        if(!finite(exact)){result.status=GravitySourceBoundStatus::Overflow;return result;}
        result.lower[i]=exact.lo;result.upper[i]=exact.hi;
        const double distance=std::max(std::abs(computed_source[i]-exact.lo),
            std::abs(computed_source[i]-exact.hi));
        result.cell_bounds[i]=distance==0.?0.:
            std::nextafter(distance,std::numeric_limits<double>::infinity());
        if(!std::isfinite(result.cell_bounds[i])) {
            result.status=GravitySourceBoundStatus::Overflow;return result;
        }
    }
    const auto norm=op.norm_interval(result.cell_bounds);
    if(norm.status!=arch::elliptic::BoundaryErrorStatus::Bounded) {
        result.status=norm.status==arch::elliptic::BoundaryErrorStatus::Overflow?
            GravitySourceBoundStatus::Overflow:GravitySourceBoundStatus::InvalidInput;return result;
    }
    result.norm_upper=norm.upper;result.status=GravitySourceBoundStatus::Bounded;return result;
}
} // namespace Physical::Gravity
