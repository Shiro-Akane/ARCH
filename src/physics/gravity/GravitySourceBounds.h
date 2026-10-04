/**
 * Internal companion certificate for the existing isolated source -4*pi*G*rho.
 * Uses authoritative stored rho/shared CGS G, mathematical pi enclosure and
 * actual computed source. Never a second source producer or simulation path.
 * Periodic mean removal/projection needs a separate certificate; reject it.
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
    Bounded,InvalidInput,UnsupportedPeriodic,CollapsedToZero,Overflow
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
} // namespace Physical::Gravity
