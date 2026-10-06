/**
 * Internal companion certificate for the existing isolated source -4*pi*G*rho.
 * Uses authoritative stored rho/shared CGS G, mathematical pi enclosure and
 * actual computed source. Never a second source producer or simulation path.
 * Periodic and isolated paths have distinct reference contracts.
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
    // Norm is conditional on stored native weights, not ideal geometry.
    double norm_upper=std::numeric_limits<double>::infinity();
};
inline GravitySourceBounds bound_isolated_gravity_source(
    const arch::elliptic::CompositePoisson& op,std::span<const double> density,
    std::span<const double> computed_source) {
    GravitySourceBounds result{};
    if(op.has_constant_nullspace()) {
        result.status=GravitySourceBoundStatus::UnsupportedPeriodic;return result;
    }
    if(density.size()!=static_cast<std::size_t>(op.size())
        ||computed_source.size()!=density.size())return result;
    const auto down=[](double v){return v<=0.?0.:std::nextafter(v,0.);};
    const auto product_upper=[](double a,double b){
        return a==0.||b==0.?0.:std::nextafter(a*b,std::numeric_limits<double>::infinity());
    };
    const double pi=arch::constants::math::pi;
    const double G=arch::constants::gravity::cgs::gravitational_constant;
    const double factor_lower=down(down(4.*down(pi))*G);
    const double factor_upper=product_upper(product_upper(4.,
        std::nextafter(pi,std::numeric_limits<double>::infinity())),G);
    result.lower.resize(density.size());result.upper.resize(density.size());
    result.cell_bounds.resize(density.size());
    for(std::size_t i=0;i<density.size();++i) {
        const double rho=density[i],actual=computed_source[i];
        if(!std::isfinite(rho)||rho<0.||!std::isfinite(actual))return result;
        if(rho>0.&&actual==0.) {
            result.status=GravitySourceBoundStatus::CollapsedToZero;return result;
        }
        const double lo=rho==0.?0.:-product_upper(factor_upper,rho);
        const double hi=rho==0.?0.:-down(factor_lower*rho);
        result.lower[i]=lo;result.upper[i]=hi;
        const double distance=std::max(std::abs(actual-lo),std::abs(actual-hi));
        result.cell_bounds[i]=distance==0.?0.:
            std::nextafter(distance,std::numeric_limits<double>::infinity());
        if(!std::isfinite(lo)||!std::isfinite(hi)||!std::isfinite(result.cell_bounds[i])) {
            result.status=GravitySourceBoundStatus::Overflow;return result;
        }
    }
    const auto norm=op.norm_interval(result.cell_bounds);
    if(norm.status!=arch::elliptic::BoundaryErrorStatus::Bounded) {
        result.status=norm.status==arch::elliptic::BoundaryErrorStatus::Overflow?
            GravitySourceBoundStatus::Overflow:GravitySourceBoundStatus::InvalidInput;
        return result;
    }
    result.norm_upper=norm.upper;result.status=GravitySourceBoundStatus::Bounded;return result;
}
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
 * The normalized mean uses stored native weights, NOT ideal geometry. Capture
 * all actual mean/subtraction/multiplication/final projection errors together.
 * Centering is an exact algebraic reference transformation, never a replacement
 * for the production source algorithm or a new density/physics definition.
 */
inline GravitySourceBounds bound_periodic_gravity_source(
    const arch::elliptic::CompositePoisson& op,std::span<const double> density,
    std::span<const double> computed_source) {
    using namespace gravity_source_detail;
    GravitySourceBounds result{};
    if(!op.has_constant_nullspace()) {
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
