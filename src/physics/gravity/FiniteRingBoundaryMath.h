/**
 * @file FiniteRingBoundaryMath.h
 * @brief Shared finite-volume ring potential math owned by GravityBoundary.
 *
 * Internal bounded mathematics, not a new solver or user precision control.
 * Quadrature differences are estimates; production certified ledger is separate.
 */
#pragma once
#include <cmath>
#include <limits>
#include <cstdint>
#include "core/ArchPortability.h"
#include "core/CompensatedSum.h"
#include "physics/constant/PhysicalConstants.h"

namespace Physical::Gravity {
enum class RingPotentialStatus : unsigned char {
    EstimatedConverged, AnalyticAxis, ExactZero, InvalidInput,
    WorkLimit, PrecisionLimit, SingularSample, Nonfinite, RuleFailure
};
struct RingPotentialResult {
    double value=std::numeric_limits<double>::quiet_NaN();
    double estimated_error=std::numeric_limits<double>::infinity();
    RingPotentialStatus status=RingPotentialStatus::InvalidInput;
    std::uint64_t kernel_evaluations=0,agm_iterations=0;
    int last_order=0;
    bool error_is_certified=false;
};
// Internal algorithm budget, never .par/schema/browser input.
struct RingQuadratureControl {
    double relative_estimate_target=1.e-9,absolute_estimate_target=0.;
    std::uint64_t maximum_kernel_evaluations=200000;
    int maximum_order=128;
};
struct EllipticKResult {
    double value=std::numeric_limits<double>::quiet_NaN();
    RingPotentialStatus status=RingPotentialStatus::InvalidInput;
    int iterations=0;
};
/** K(m)=pi/(2 AGM(1,sqrt(1-m))). Argument is complementary root, not m or k. */
ARCH_HEAVY_INLINE EllipticKResult ring_elliptic_k_complementary_root(double root) {
    EllipticKResult result{};
    if(!std::isfinite(root) || root<0. || root>1.)return result;
    if(root==0.) {result.status=RingPotentialStatus::SingularSample;return result;}
    double a=1.,b=root;
    for(int i=0;i<32;++i) {
        const double next_a=.5*(a+b),next_b=std::sqrt(a*b);
        ++result.iterations;
        a=next_a;b=next_b;
        if(a-b<=4*std::numeric_limits<double>::epsilon()*a) {
            result.value=arch::constants::math::pi/(2*a);
            result.status=RingPotentialStatus::EstimatedConverged;return result;
        }
    }
    result.status=RingPotentialStatus::WorkLimit;return result;
}
/** Certified enclosure of K for an exact stored complementary root.
 * Workflow: same ring kernel owner, prior to certified source quadrature.
 * Preconditions: IEEE binary64 basic/sqrt operations, gradual underflow,
 * no fast-math/contraction (the project's host/device build contract).
 * This does NOT bound rounded distance/root construction or a source integral.
 */
enum class RingIntervalStatus : unsigned char {
    Bounded, InvalidInput, SingularSample, PrecisionLimit, WorkLimit
};
struct EllipticKInterval {
    double lower=0.,upper=std::numeric_limits<double>::infinity();
    RingIntervalStatus status=RingIntervalStatus::InvalidInput;
    int iterations=0;
};
namespace finite_ring_detail {
ARCH_INLINE double positive_down(double x) {
    return x<=0. ? 0. : std::nextafter(x,0.);
}
ARCH_INLINE double positive_up(double x) {
    return x==0. ? 0. : std::nextafter(x,std::numeric_limits<double>::infinity());
}
ARCH_INLINE double product_up(double a,double b) {
    if(a==0. || b==0.)return 0.;
    return std::nextafter(a*b,std::numeric_limits<double>::infinity());
}
} // namespace finite_ring_detail
ARCH_HEAVY_INLINE EllipticKInterval ring_elliptic_k_interval(double root) {
    using namespace finite_ring_detail;
    EllipticKInterval result{};
    if(!std::isfinite(root) || root<0. || root>1.)return result;
    if(root==0.) {result.status=RingIntervalStatus::SingularSample;return result;}
    static_assert(std::numeric_limits<double>::is_iec559
        && std::numeric_limits<double>::digits==53);
    double al=1.,au=1.,bl=root,bu=root;
    const double pil=positive_down(arch::constants::math::pi);
    const double piu=positive_up(arch::constants::math::pi);
    // AGM b_n <= M <= a_n; separately enclose both exact iterate sequences.
    for(int step=0;step<32;++step) {
        result.lower=positive_down(pil/product_up(2.,au));
        if(bl>0.)result.upper=positive_up(piu/positive_down(2.*bl));
        if(std::isfinite(result.upper) && result.upper-result.lower
            <=64*std::numeric_limits<double>::epsilon()*result.lower) {
            result.status=RingIntervalStatus::Bounded;return result;
        }
        const double next_al=positive_down(.5*positive_down(al+bl));
        const double next_au=positive_up(.5*positive_up(au+bu));
        // sqrt(a)*sqrt(b) avoids an underflowing a*b before sqrt.
        const double next_bl=positive_down(positive_down(std::sqrt(al))
            *positive_down(std::sqrt(bl)));
        const double next_bu=product_up(positive_up(std::sqrt(au)),
            positive_up(std::sqrt(bu)));
        ++result.iterations;
        if(next_al==al && next_au==au && next_bl==bl && next_bu==bu) {
            result.status=RingIntervalStatus::PrecisionLimit;return result;
        }
        al=next_al;au=next_au;bl=next_bl;bu=next_bu;
    }
    result.status=RingIntervalStatus::WorkLimit;return result;
}
namespace finite_ring_detail {
/** Integral kernel r K(m)/s. Known offsets avoid subtracting a near-contact point twice. */
ARCH_HEAVY_INLINE EllipticKResult kernel(double observer_r,double source_r,
    double radial_offset,double axial_offset) {
    EllipticKResult result{};
    const double s=std::hypot(observer_r+source_r,axial_offset);
    const double d=std::hypot(radial_offset,axial_offset);
    if(!std::isfinite(s) || !std::isfinite(d) || !(s>0.)) {
        result.status=RingPotentialStatus::Nonfinite;return result;
    }
    // sqrt(1-m)=d/s from direct distances. Do not form 1-4Rr/s^2;
    // retaining the root also avoids underflow of a positive (d/s)^2.
    result=ring_elliptic_k_complementary_root(d/s);
    if(result.status!=RingPotentialStatus::EstimatedConverged)return result;
    result.value=(source_r/s)*result.value;
    if(!std::isfinite(result.value))result.status=RingPotentialStatus::Nonfinite;
    return result;
}
ARCH_HEAVY_INLINE bool gauss_rule(int order,double* nodes,double* weights) {
    const double eps=std::numeric_limits<double>::epsilon();
    for(int i=0;i<order/2;++i) {
        double x=std::cos(arch::constants::math::pi*(i+.75)/(order+.5));
        bool converged=false;
        for(int it=0;it<32;++it) {
            double p0=1.,p1=x;
            for(int n=2;n<=order;++n) {
                const double next=((2*n-1)*x*p1-(n-1)*p0)/n;
                p0=p1;p1=next;
            }
            const double derivative=order*(x*p1-p0)/(x*x-1.);
            const double next=x-p1/derivative;
            if(std::abs(next-x)<=4*eps) {x=next;converged=true;break;}
            x=next;
        }
        if(!converged)return false;
        double p0=1.,p1=x;
        for(int n=2;n<=order;++n) {
            const double next=((2*n-1)*x*p1-(n-1)*p0)/n;p0=p1;p1=next;
        }
        const double derivative=order*(x*p1-p0)/(x*x-1.);
        const double weight=2/((1-x*x)*derivative*derivative);
        if(!std::isfinite(weight) || !(weight>0.) || !(x>0. && x<1.))return false;
        nodes[i]=-x;nodes[order-1-i]=x;
        weights[i]=weights[order-1-i]=weight;
    }
    return true;
}
/** Stable asinh(upper/a)-asinh(lower/a), without subtracting near equal logs. */
ARCH_INLINE double asinh_interval(double a,double lower,double upper) {
    if(a==0.)return 0.; // Multiplied by a^2 in the continuous primitive.
    if(upper<=0.) {const double mirrored_lower=-upper;upper=-lower;lower=mirrored_lower;}
    if(lower<0.)return std::asinh(upper/a)-std::asinh(lower/a);
    const double hi=std::hypot(a,upper),lo=std::hypot(a,lower);
    const double dz=upper-lower;
    return std::log1p(dz*(1+(upper+lower)/(hi+lo))/(lower+lo));
}
ARCH_INLINE double axis_section(double left,double right,double offset) {
    return ((right-left)*(right+left))
        /(std::hypot(right,offset)+std::hypot(left,offset));
}
ARCH_HEAVY_INLINE RingPotentialResult axis(double lo,double hi,double zl,double zh,
    double density,double zo,double G,const RingQuadratureControl& control) {
    RingPotentialResult result{};
    const double lower=zl-zo,upper=zh-zo;
    const double terms[]{
        upper*axis_section(lo,hi,upper),
        -lower*axis_section(lo,hi,lower),
        hi*hi*asinh_interval(hi,lower,upper),
        -lo*lo*asinh_interval(lo,lower,upper)};
    arch::math::CompensatedSum sum,magnitude;
    for(double term:terms) {sum.add(term);magnitude.add(std::abs(term));}
    const double factor=arch::constants::math::pi*G*density;
    result.value=-factor*sum.value();
    // Roundoff diagnostic only; not a proven interval/arithmetic bound.
    result.estimated_error=64*std::numeric_limits<double>::epsilon()
        *std::abs(factor)*magnitude.value();
    if(!std::isfinite(result.value) || !std::isfinite(result.estimated_error))
        result.status=RingPotentialStatus::Nonfinite;
    else result.status=result.estimated_error<=control.absolute_estimate_target
            +control.relative_estimate_target*std::abs(result.value)
        ?RingPotentialStatus::AnalyticAxis:RingPotentialStatus::PrecisionLimit;
    return result;
}
} // namespace finite_ring_detail

/** Point potential of a uniform full ring; no force path, averaging or softening. */
ARCH_HEAVY_INLINE RingPotentialResult finite_ring_potential_estimate(
    double rlo,double rhi,double zlo,double zhi,double density,
    double observer_r,double observer_z,double G,RingQuadratureControl control={}) {
    RingPotentialResult result{};
    if(!std::isfinite(rlo) || !std::isfinite(rhi) || !(0.<=rlo && rlo<rhi)
        || !std::isfinite(zlo) || !std::isfinite(zhi) || !(zlo<zhi)
        || !std::isfinite(density) || density<0.
        || !std::isfinite(observer_r) || observer_r<0. || !std::isfinite(observer_z)
        || !std::isfinite(G) || G<=0.
        || !std::isfinite(control.relative_estimate_target) || control.relative_estimate_target<0.
        || !std::isfinite(control.absolute_estimate_target) || control.absolute_estimate_target<0.
        || control.maximum_order<8 || control.maximum_order>128)return result;
    if(density==0.) {
        result.value=result.estimated_error=0.;result.status=RingPotentialStatus::ExactZero;return result;
    }
    if(observer_r==0.)
        return finite_ring_detail::axis(rlo,rhi,zlo,zhi,density,observer_z,G,control);
    const bool contact=rlo<=observer_r && observer_r<=rhi
        && zlo<=observer_z && observer_z<=zhi;
    double rs[3]{rlo,rhi,rhi},zs[3]{zlo,zhi,zhi};int nr=1,nz=1;
    if(contact) {
        if(rlo<observer_r && observer_r<rhi) {rs[1]=observer_r;nr=2;}
        if(zlo<observer_z && observer_z<zhi) {zs[1]=observer_z;nz=2;}
    }
    double previous=std::numeric_limits<double>::quiet_NaN();
    for(int order=4;order<=control.maximum_order;order*=2) {
        double nodes[128]{},weights[128]{};
        if(!finite_ring_detail::gauss_rule(order,nodes,weights)) {
            result.status=RingPotentialStatus::RuleFailure;return result;
        }
        arch::math::CompensatedSum integral;
        for(int ir=0;ir<nr;++ir)for(int iz=0;iz<nz;++iz)
            for(int triangle=0;triangle<(contact?2:1);++triangle)
                for(int i=0;i<order;++i)for(int j=0;j<order;++j) {
                    if(result.kernel_evaluations>=control.maximum_kernel_evaluations) {
                        result.status=RingPotentialStatus::WorkLimit;return result;
                    }
                    const double t=.5*(1+nodes[i]),u=.5*(1+nodes[j]);
                    double radius,dr,dz,jacobian;
                    if(contact) {
                        // Each partition rectangle has the observer as a vertex.
                        // Triangles (0,(dr,0),(dr,dz)) and (0,(dr,dz),(0,dz)).
                        const double a=(rs[ir]==observer_r?rs[ir+1]:rs[ir])-observer_r;
                        const double b=(zs[iz]==observer_z?zs[iz+1]:zs[iz])-observer_z;
                        if(a==0. || b==0.)continue;
                        dr=t*(triangle==0?a:(1-u)*a);
                        dz=t*(triangle==0?u*b:b);
                        radius=observer_r+dr;
                        jacobian=t*std::abs(a*b);
                    } else {
                        radius=rlo+t*(rhi-rlo);dr=radius-observer_r;
                        dz=zlo+u*(zhi-zlo)-observer_z;
                        jacobian=(rhi-rlo)*(zhi-zlo);
                    }
                    // Degenerate triangles are removed geometrically, never via epsilon.
                    if(!(jacobian>0.) || !std::isfinite(jacobian)) {
                        result.status=RingPotentialStatus::PrecisionLimit;return result;
                    }
                    const auto value=finite_ring_detail::kernel(observer_r,radius,dr,dz);
                    ++result.kernel_evaluations;result.agm_iterations+=value.iterations;
                    if(value.status!=RingPotentialStatus::EstimatedConverged) {
                        result.status=value.status;return result;
                    }
                    integral.add(.25*weights[i]*weights[j]*jacobian*value.value);
                }
        const double current=-4*G*density*integral.value();
        if(!std::isfinite(current)) {result.status=RingPotentialStatus::Nonfinite;return result;}
        result.value=current;result.last_order=order;
        if(order>4) {
            result.estimated_error=std::abs(current-previous);
            if(result.estimated_error<=control.absolute_estimate_target
                +control.relative_estimate_target*std::abs(current)) {
                result.status=RingPotentialStatus::EstimatedConverged;return result;
            }
        }
        previous=current;
    }
    result.status=RingPotentialStatus::WorkLimit;return result;
}
} // namespace Physical::Gravity
