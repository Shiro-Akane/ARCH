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
#include <vector>
#include <algorithm>
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

/** Reliable range integration; host owns subdivision resources, shared leaves
 * own K/distance/log mathematics. No second gravity solver or user knob.
 */
struct RingEnclosureControl {
    double relative_target=0.,absolute_target=0.;
    std::uint64_t maximum_boxes=1024;
};
struct RingPotentialEnclosure {
    double lower=std::numeric_limits<double>::quiet_NaN();
    double upper=std::numeric_limits<double>::quiet_NaN();
    double value=std::numeric_limits<double>::quiet_NaN();
    double absolute_error=std::numeric_limits<double>::infinity();
    RingIntervalStatus status=RingIntervalStatus::InvalidInput;
    std::uint64_t leaf_boxes=0,range_evaluations=0,kernel_enclosures=0,agm_iterations=0;
    bool bound_valid=false;
};
namespace finite_ring_detail {
struct PositiveInterval {double lower=0.,upper=0.;};
ARCH_INLINE double quotient_up(double a,double b) {
    if(a==0.)return 0.;
    return std::nextafter(a/b,std::numeric_limits<double>::infinity());
}
ARCH_INLINE double sum_up(double a,double b) {
    return a==0. && b==0. ? 0. : positive_up(a+b);
}
/** Lower/upper hypot of nonnegative exact stored inputs, without squaring scales. */
ARCH_HEAVY_INLINE PositiveInterval distance_interval(double x,double y) {
    const double scale=x>y?x:y;
    if(scale==0.)return {};
    const double xl=positive_down(x/scale),yl=positive_down(y/scale);
    const double xu=quotient_up(x,scale),yu=quotient_up(y,scale);
    const double lo=positive_down(positive_down(xl*xl)+positive_down(yl*yl));
    const double hi=sum_up(product_up(xu,xu),product_up(yu,yu));
    return {positive_down(scale*positive_down(std::sqrt(lo))),
            product_up(scale,positive_up(std::sqrt(hi)))};
}
ARCH_INLINE PositiveInterval absolute_offset_range(double lo,double hi,double observer) {
    double nearest=0.;
    if(observer<lo)nearest=positive_down(lo-observer);
    else if(observer>hi)nearest=positive_down(observer-hi);
    const double a=lo==observer?0.:positive_up(std::abs(lo-observer));
    const double b=hi==observer?0.:positive_up(std::abs(hi-observer));
    return {nearest,a>b?a:b};
}
/** Upper log via positive atanh series, including an explicit geometric tail.
 * z in [0, 1/3+roundoff]. No std::log accuracy assumption in a certificate.
 */
ARCH_HEAVY_INLINE double log_series_upper(double z) {
    if(z==0.)return 0.;
    const double z2=product_up(z,z);
    double power=z,sum=0.;
    for(int k=0;k<48;++k) {
        sum=sum_up(sum,quotient_up(power,2*k+1.));
        power=product_up(power,z2);
    }
    const double denominator=positive_down(97.*positive_down(1.-z2));
    return product_up(2.,sum_up(sum,quotient_up(power,denominator)));
}
ARCH_HEAVY_INLINE double logarithm_upper(double x) {
    if(!(x>=1.) || !std::isfinite(x))return std::numeric_limits<double>::infinity();
    if(x==1.)return 0.;
    int exponent=0;
    const double m=2*std::frexp(x,&exponent); // Exact binary scaling; 1<=m<2.
    --exponent;
    const double z=quotient_up(positive_up(m-1.),positive_down(m+1.));
    const double ln2=log_series_upper(quotient_up(1.,3.));
    return sum_up(product_up(static_cast<double>(exponent),ln2),log_series_upper(z));
}
/** Lower positive atanh-series partial sum; omitted tail is positive. */
ARCH_HEAVY_INLINE double log_series_lower(double z) {
    if(z==0.)return 0.;
    const double z2=positive_down(z*z);
    double power=z,sum=0.;
    for(int k=0;k<48;++k) {
        sum=positive_down(sum+positive_down(power/(2*k+1.)));
        power=positive_down(power*z2);
    }
    return positive_down(2.*sum);
}
ARCH_HEAVY_INLINE double logarithm_lower(double x) {
    if(!(x>=1.) || !std::isfinite(x))return std::numeric_limits<double>::quiet_NaN();
    if(x==1.)return 0.;
    int exponent=0;
    const double m=2*std::frexp(x,&exponent);--exponent;
    const double z=positive_down(positive_down(m-1.)/positive_up(m+1.));
    const double ln2=log_series_lower(positive_down(1./3.));
    return positive_down(positive_down(static_cast<double>(exponent)*ln2)+log_series_lower(z));
}
struct SignedInterval {double lower=0.,upper=0.;};
ARCH_INLINE SignedInterval interval_invalid() {
    return {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()};
}
ARCH_INLINE bool interval_finite(SignedInterval a) {
    return std::isfinite(a.lower)&&std::isfinite(a.upper)&&a.lower<=a.upper;
}
ARCH_INLINE SignedInterval interval_sum(SignedInterval a,SignedInterval b) {
    if(!interval_finite(a)||!interval_finite(b))return interval_invalid();
    if(a.lower==0.&&a.upper==0.)return b;
    if(b.lower==0.&&b.upper==0.)return a;
    return {std::nextafter(a.lower+b.lower,-std::numeric_limits<double>::infinity()),
            std::nextafter(a.upper+b.upper,std::numeric_limits<double>::infinity())};
}
ARCH_INLINE SignedInterval interval_negate(SignedInterval a) {return {-a.upper,-a.lower};}
ARCH_INLINE SignedInterval interval_product(SignedInterval a,SignedInterval b) {
    if(!interval_finite(a)||!interval_finite(b))return interval_invalid();
    if((a.lower==0.&&a.upper==0.)||(b.lower==0.&&b.upper==0.))return {};
    const double values[]{a.lower*b.lower,a.lower*b.upper,a.upper*b.lower,a.upper*b.upper};
    double lo=values[0],hi=values[0];
    for(double v:values) {
        if(!std::isfinite(v))return interval_invalid();
        lo=std::min(lo,v);hi=std::max(hi,v);
    }
    return {std::nextafter(lo,-std::numeric_limits<double>::infinity()),
            std::nextafter(hi,std::numeric_limits<double>::infinity())};
}
ARCH_INLINE SignedInterval interval_quotient_positive(SignedInterval a,SignedInterval b) {
    if(!interval_finite(a)||!interval_finite(b)||!(b.lower>0.))return interval_invalid();
    const SignedInterval reciprocal{positive_down(1./b.upper),quotient_up(1.,b.lower)};
    return interval_product(a,reciprocal);
}
ARCH_INLINE SignedInterval offset_interval(double a,double b) {
    if(a==b)return {};
    const double difference=a-b;
    return {std::nextafter(difference,-std::numeric_limits<double>::infinity()),
            std::nextafter(difference,std::numeric_limits<double>::infinity())};
}
ARCH_HEAVY_INLINE SignedInterval axis_hypot_interval(double radius,SignedInterval u) {
    if(!interval_finite(u))return interval_invalid();
    const double min_abs=u.lower<=0.&&u.upper>=0.?0.:
        std::min(std::abs(u.lower),std::abs(u.upper));
    const double max_abs=std::max(std::abs(u.lower),std::abs(u.upper));
    return {distance_interval(radius,min_abs).lower,distance_interval(radius,max_abs).upper};
}
/** Continuous odd asinh via enclosed hypot and enclosed positive log. */
ARCH_HEAVY_INLINE SignedInterval asinh_point_interval(double x) {
    if(!std::isfinite(x))return interval_invalid();
    if(x==0.)return {};
    const double magnitude=std::abs(x);
    const auto hyp=distance_interval(1.,magnitude);
    const double lo=positive_down(magnitude+hyp.lower);
    const double hi=sum_up(magnitude,hyp.upper);
    // Exact asinh argument >=1; outward lower arithmetic may lie just below it.
    SignedInterval result{logarithm_lower(std::max(1.,lo)),logarithm_upper(hi)};
    return x<0.?interval_negate(result):result;
}
ARCH_HEAVY_INLINE SignedInterval asinh_interval_enclosure(double radius,SignedInterval u) {
    if(!interval_finite(u)||!(radius>0.))return interval_invalid();
    const auto q=interval_quotient_positive(u,{radius,radius});
    if(!interval_finite(q))return interval_invalid();
    const auto lo=asinh_point_interval(q.lower),hi=asinh_point_interval(q.upper);
    return {lo.lower,hi.upper};
}
/** log(1+x), x>=0, without rounding tiny x out of 1+x.
 * The existing positive atanh series uses z=x/(2+x) for x<=1;
 * larger arguments reuse binary exponent reduction. No libm log assumption.
 */
ARCH_HEAVY_INLINE SignedInterval log1p_enclosure(SignedInterval x) {
    if(!interval_finite(x)||x.lower<0.)return interval_invalid();
    if(x.upper<=1.) {
        const double zl=positive_down(x.lower/positive_up(2.+x.lower));
        const double zu=quotient_up(x.upper,positive_down(2.+x.upper));
        return {log_series_lower(zl),log_series_upper(zu)};
    }
    const double lo=std::max(1.,positive_down(1.+x.lower));
    const double hi=sum_up(1.,x.upper);
    return {logarithm_lower(lo),logarithm_upper(hi)};
}
/** Enclose integral dz/hypot(radius,z-zo). Same-side endpoints use the
 * exact source width, not the subtraction of independently rounded asinh.
 * Negative intervals mirror by odd asinh; crossing zero adds positive terms.
 */
ARCH_HEAVY_INLINE SignedInterval axis_asinh_difference_enclosure(
    double radius,double zl,double zh,double zo) {
    if(!(radius>0.))return interval_invalid();
    auto lower=offset_interval(zl,zo),upper=offset_interval(zh,zo);
    if(!interval_finite(lower)||!interval_finite(upper))return interval_invalid();
    if(upper.upper<=0.) {
        const auto original_lower=lower;
        lower=interval_negate(upper);upper=interval_negate(original_lower);
    }
    if(lower.lower<0.)
        return interval_sum(asinh_interval_enclosure(radius,upper),
            interval_negate(asinh_interval_enclosure(radius,lower)));
    const auto hi=axis_hypot_interval(radius,upper),lo=axis_hypot_interval(radius,lower);
    const auto correction=interval_quotient_positive(interval_sum(upper,lower),
        interval_sum(hi,lo));
    const auto numerator=interval_product(offset_interval(zh,zl),
        interval_sum({1.,1.},correction));
    auto ratio=interval_quotient_positive(numerator,interval_sum(lower,lo));
    if(!interval_finite(ratio))return interval_invalid();
    ratio.lower=std::max(0.,ratio.lower); // Exact ratio is nonnegative.
    return log1p_enclosure(ratio);
}
/** Same factored axis section as the existing analytic estimate. */
ARCH_HEAVY_INLINE SignedInterval axis_section_enclosure(double rl,double rh,SignedInterval u) {
    const auto delta=offset_interval(rh,rl);
    const auto sum=interval_sum({rh,rh},{rl,rl});
    const auto denominator=interval_sum(axis_hypot_interval(rh,u),axis_hypot_interval(rl,u));
    return interval_quotient_positive(interval_product(delta,sum),denominator);
}
ARCH_HEAVY_INLINE SignedInterval axis_radial_term_enclosure(
    double radius,SignedInterval lower,SignedInterval upper) {
    if(radius==0.)return {}; // continuous r² asinh(u/r) limit.
    const auto difference=interval_sum(asinh_interval_enclosure(radius,upper),
        interval_negate(asinh_interval_enclosure(radius,lower)));
    return interval_product(interval_product({radius,radius},{radius,radius}),difference);
}
ARCH_HEAVY_INLINE SignedInterval axis_integral_potential_enclosure(
    SignedInterval integral,double density,double G) {
    const SignedInterval pi_range{positive_down(arch::constants::math::pi),
                                  positive_up(arch::constants::math::pi)};
    const auto factor=interval_product(interval_product(
        interval_product({2.,2.},pi_range),{G,G}),{density,density});
    return interval_negate(interval_product(factor,integral));
}
/** Independent axis enclosure after exact radial integration.
 * f(u)=sqrt(rh²+u²)-sqrt(rl²+u²)=integral_rl^rh r/hypot(r,u) dr.
 * |f''(u)| <= 2 integral r/(r²+u²)^(3/2) dr <= (rh²-rl²)/dmin³.
 * The midpoint integral error is therefore <= dz³*(rh²-rl²)/(24*dmin³).
 * This is a proved derivative remainder, not a difference between quadratures.
 * It tightens the same axis point potential; no near-axis switch or new force.
 */
ARCH_HEAVY_INLINE SignedInterval axis_midpoint_potential_enclosure(
    double rl,double rh,double zl,double zh,double density,double zo,double G) {
    const auto dz=offset_interval(zh,zl);
    const auto radial_square=interval_product(offset_interval(rh,rl),
        interval_sum({rh,rh},{rl,rl}));
    const auto offsets=absolute_offset_range(zl,zh,zo);
    const double distance_lower=distance_interval(rl,offsets.lower).lower;
    if(!(distance_lower>0.)||!std::isfinite(distance_lower))return interval_invalid();
    // Enclose the exact geometric midpoint, including offset and width rounding.
    const auto midpoint=interval_sum(offset_interval(zl,zo),interval_product(dz,{.5,.5}));
    const auto midpoint_integral=interval_product(dz,axis_section_enclosure(rl,rh,midpoint));
    // Factored remainder avoids cubing large distances or tiny source widths.
    const double ratio=quotient_up(dz.upper,distance_lower);
    const double scale=quotient_up(product_up(dz.upper,radial_square.upper),distance_lower);
    const double error=quotient_up(product_up(scale,product_up(ratio,ratio)),24.);
    if(!interval_finite(midpoint_integral)||!std::isfinite(error))return interval_invalid();
    auto integral=interval_sum(midpoint_integral,{-error,error});
    if(!interval_finite(integral))return interval_invalid();
    integral.lower=std::max(0.,integral.lower); // Exact nonnegative Newton integrand.
    return axis_integral_potential_enclosure(integral,density,G);
}
/** Independent complementary bound after exact axial integration.
 * g(r)=integral r/hypot(r,u) du, |g''|<=3*rh*dz/dmin³.
 * Radial midpoint remainder <= dr³*rh*dz/(8*dmin³).
 * The asinh gap decreases monotonically with r, enclosing an exact
 * midpoint radius even when it lies between adjacent stored doubles.
 */
ARCH_HEAVY_INLINE SignedInterval axis_radial_midpoint_potential_enclosure(
    double rl,double rh,double zl,double zh,double density,double zo,double G) {
    const auto dr=offset_interval(rh,rl),dz=offset_interval(zh,zl);
    const auto midpoint=interval_sum({rl,rl},interval_product(dr,{.5,.5}));
    if(!interval_finite(midpoint)||!(midpoint.lower>0.))return interval_invalid();
    const auto offsets=absolute_offset_range(zl,zh,zo);
    const double distance_lower=distance_interval(rl,offsets.lower).lower;
    if(!(distance_lower>0.)||!std::isfinite(distance_lower))return interval_invalid();
    const auto low_gap=axis_asinh_difference_enclosure(midpoint.upper,zl,zh,zo);
    const auto high_gap=axis_asinh_difference_enclosure(midpoint.lower,zl,zh,zo);
    if(!interval_finite(low_gap)||!interval_finite(high_gap))return interval_invalid();
    const SignedInterval gap{std::max(0.,low_gap.lower),high_gap.upper};
    const auto center=interval_product(dr,interval_product(midpoint,gap));
    const double ratio=quotient_up(dr.upper,distance_lower);
    const double scale=quotient_up(product_up(dr.upper,product_up(rh,dz.upper)),distance_lower);
    const double error=quotient_up(product_up(scale,product_up(ratio,ratio)),8.);
    if(!interval_finite(center)||!std::isfinite(error))return interval_invalid();
    auto integral=interval_sum(center,{-error,error});
    if(!interval_finite(integral))return interval_invalid();
    integral.lower=std::max(0.,integral.lower);
    return axis_integral_potential_enclosure(integral,density,G);
}
/** Analytic axis potential, same four terms as axis(), with arithmetic ledger.
 * This is the R_o=0 branch, not a small-radius numerical switch.
 */
ARCH_HEAVY_INLINE RingPotentialEnclosure axis_potential_enclosure(
    double rl,double rh,double zl,double zh,double density,double zo,double G,
    const RingEnclosureControl& control) {
    RingPotentialEnclosure result{};
    const auto lower=offset_interval(zl,zo),upper=offset_interval(zh,zo);
    SignedInterval total=interval_sum(
        interval_product(upper,axis_section_enclosure(rl,rh,upper)),
        interval_negate(interval_product(lower,axis_section_enclosure(rl,rh,lower))));
    total=interval_sum(total,axis_radial_term_enclosure(rh,lower,upper));
    total=interval_sum(total,interval_negate(axis_radial_term_enclosure(rl,lower,upper)));
    const SignedInterval pi_range{positive_down(arch::constants::math::pi),
                                  positive_up(arch::constants::math::pi)};
    const auto factor=interval_product(interval_product(pi_range,{G,G}),{density,density});
    auto potential=interval_negate(interval_product(factor,total));
    const SignedInterval independent[]{
        axis_midpoint_potential_enclosure(rl,rh,zl,zh,density,zo,G),
        axis_radial_midpoint_potential_enclosure(rl,rh,zl,zh,density,zo,G)};
    for(const auto candidate:independent)if(interval_finite(candidate)) {
        if(interval_finite(potential)) {
            potential.lower=std::max(potential.lower,candidate.lower);
            potential.upper=std::min(potential.upper,candidate.upper);
            if(potential.lower>potential.upper) {
                result.status=RingIntervalStatus::PrecisionLimit;return result;
            } // Disjoint certified intervals cannot be overwritten by a later bound.
        } else potential=candidate;
    }
    // Nonnegative source has nonpositive potential. This exact sign tightens
    // arithmetic overestimation, never clips physical field values.
    if(interval_finite(potential))potential.upper=std::min(0.,potential.upper);
    if(!interval_finite(potential)) {
        result.status=RingIntervalStatus::PrecisionLimit;return result;
    }
    result.lower=potential.lower;result.upper=potential.upper;
    result.value=result.lower+.5*(result.upper-result.lower);
    result.absolute_error=positive_up(std::max(result.value-result.lower,result.upper-result.value));
    result.bound_valid=std::isfinite(result.value)&&std::isfinite(result.absolute_error);
    if(!result.bound_valid) {result.status=RingIntervalStatus::PrecisionLimit;return result;}
    const double target=std::max(control.absolute_target,
        positive_down(control.relative_target*std::abs(result.value)));
    result.status=result.absolute_error<=target?RingIntervalStatus::Bounded:
        RingIntervalStatus::PrecisionLimit;
    return result;
}
/** Complete finite leaf about its true center (0,0,(zl+zh)/2).
 * Only this uniform leaf has inversion symmetry; no parent inference.
 * Monopole + second moments, with exact Legendre tail <= GM/R*q^4/(1-q^2).
 * All geometry/moment/evaluation arithmetic is enclosed, not just the tail.
 * Return invalid inside the complete 3D support sphere; near/contact stays
 * with the existing source-integral owner.
 */
ARCH_HEAVY_INLINE SignedInterval single_leaf_far_potential_enclosure(
    double rl,double rh,double zl,double zh,double density,double ro,double zo,double G) {
    const auto dr=offset_interval(rh,rl),dz=offset_interval(zh,zl);
    const auto center=interval_sum({zl,zl},interval_product(dz,{.5,.5}));
    const auto u=interval_sum({zo,zo},interval_negate(center));
    const auto distance=axis_hypot_interval(ro,u);
    const auto support=distance_interval(rh,product_up(.5,dz.upper));
    if(!interval_finite(distance)||!(distance.lower>support.upper))
        return interval_invalid();
    const double q=quotient_up(support.upper,distance.lower);
    const double q2=product_up(q,q);
    const double denominator=positive_down(1.-q2);
    if(!(denominator>0.))return interval_invalid();
    const SignedInterval pi_range{positive_down(arch::constants::math::pi),
                                  positive_up(arch::constants::math::pi)};
    const auto mass=interval_product(interval_product(
        interval_product(interval_product(pi_range,{density,density}),dr),
        interval_sum({rh,rh},{rl,rl})),dz);
    const auto radial_square=interval_sum(interval_product({rh,rh},{rh,rh}),
                                         interval_product({rl,rl},{rl,rl}));
    const auto ixx=interval_product(interval_product(mass,radial_square),{.25,.25});
    const auto izz=interval_quotient_positive(
        interval_product(mass,interval_product(dz,dz)),{12.,12.});
    const auto inverse=interval_quotient_positive({1.,1.},distance);
    const auto nr=interval_product({ro,ro},inverse),nz=interval_product(u,inverse);
    const auto projected=interval_sum(interval_product(ixx,interval_product(nr,nr)),
                                      interval_product(izz,interval_product(nz,nz)));
    const auto trace=interval_sum(interval_product({2.,2.},ixx),izz);
    const auto quadrupole=interval_product(interval_product(
        interval_sum(interval_product({3.,3.},projected),interval_negate(trace)),
        {.5,.5}),interval_product(inverse,interval_product(inverse,inverse)));
    const auto truncated=interval_negate(interval_product({G,G},
        interval_sum(interval_product(mass,inverse),quadrupole)));
    if(!interval_finite(mass)||mass.lower<0.||!interval_finite(truncated))
        return interval_invalid();
    const double leading=product_up(G,quotient_up(mass.upper,distance.lower));
    const double tail=product_up(leading,quotient_up(product_up(q2,q2),denominator));
    if(!std::isfinite(tail))return interval_invalid();
    auto potential=interval_sum(truncated,{-tail,tail});
    if(interval_finite(potential))potential.upper=std::min(0.,potential.upper);
    return potential;
}
/** Range of r*K(d/s)/s for enclosed exact quadrature coordinates.
 * Positive separation only; singular samples are never shifted or softened.
 */
ARCH_HEAVY_INLINE SignedInterval separated_kernel_enclosure(
    SignedInterval radius,SignedInterval axial,double ro,double zo,
    std::uint64_t* kernel_count=nullptr,std::uint64_t* agm_count=nullptr) {
    if(!interval_finite(radius)||!interval_finite(axial)||radius.lower<0.)
        return interval_invalid();
    const auto radial=absolute_offset_range(radius.lower,radius.upper,ro);
    const auto vertical=absolute_offset_range(axial.lower,axial.upper,zo);
    const auto dmin=distance_interval(radial.lower,vertical.lower);
    const auto dmax=distance_interval(radial.upper,vertical.upper);
    const auto smin=distance_interval(positive_down(ro+radius.lower),vertical.lower);
    const auto smax=distance_interval(sum_up(ro,radius.upper),vertical.upper);
    if(!(dmin.lower>0.)||!(smin.lower>0.)||!std::isfinite(smax.upper))
        return interval_invalid();
    const double qlo=positive_down(dmin.lower/smax.upper);
    const double qhi=std::min(1.,quotient_up(dmax.upper,smin.lower));
    if(!(qlo>0.))return interval_invalid();
    const auto high=ring_elliptic_k_interval(qlo),low=ring_elliptic_k_interval(qhi);
    if(kernel_count)*kernel_count+=2;
    if(agm_count)*agm_count+=high.iterations+low.iterations;
    if(high.status!=RingIntervalStatus::Bounded||low.status!=RingIntervalStatus::Bounded)
        return interval_invalid();
    return {positive_down(positive_down(radius.lower/smax.upper)*low.lower),
            product_up(quotient_up(radius.upper,smin.lower),high.upper)};
}
/** Two-point tensor Gauss with a proved fourth derivative remainder.
 * From the Newton angular integral, |d^m(1/D)/dx^m| <= m!/d^(m+1).
 * h=r*K/s=(1/4)integral r/D dtheta:
 * |h_rrrr| <= (pi/2)*4!*(rh/d^5+1/d^4),
 * |h_zzzz| <= (pi/2)*4!*rh/d^5.
 * Tensor positive weights give area*pi/360 times the dimensionless sum
 * below. Node 1/sqrt(3), geometry, kernel and reduction are all enclosed.
 * This is not a difference between quadratures or an inferred tolerance.
 */
ARCH_HEAVY_INLINE SignedInterval separated_gauss2_integral_enclosure(
    double rl,double rh,double zl,double zh,double ro,double zo,double d_lower,
    std::uint64_t* kernel_count=nullptr,std::uint64_t* agm_count=nullptr) {
    if(!(d_lower>0.))return interval_invalid();
    const auto dr=offset_interval(rh,rl),dz=offset_interval(zh,zl);
    const auto hr=interval_product(dr,{.5,.5}),hz=interval_product(dz,{.5,.5});
    const auto mr=interval_sum({rl,rl},hr),mz=interval_sum({zl,zl},hz);
    const SignedInterval root{positive_down(std::sqrt(3.)),positive_up(std::sqrt(3.))};
    const auto node=interval_quotient_positive({1.,1.},root);
    SignedInterval sum{};
    for(double sign_r:{-1.,1.})for(double sign_z:{-1.,1.}) {
        const auto radius=interval_sum(mr,interval_product(hr,
            interval_product(node,{sign_r,sign_r})));
        const auto axial=interval_sum(mz,interval_product(hz,
            interval_product(node,{sign_z,sign_z})));
        const auto kernel=separated_kernel_enclosure(radius,axial,ro,zo,kernel_count,agm_count);
        if(!interval_finite(kernel))return interval_invalid();
        sum=interval_sum(sum,kernel);
    }
    const auto area=interval_product(dr,dz);
    const auto quadrature=interval_product(interval_product(area,{.25,.25}),sum);
    const double qr=quotient_up(dr.upper,d_lower),qz=quotient_up(dz.upper,d_lower);
    const double qr2=product_up(qr,qr),qz2=product_up(qz,qz);
    const double radial=quotient_up(rh,d_lower);
    const double remainder=product_up(quotient_up(
        product_up(area.upper,positive_up(arch::constants::math::pi)),360.),
        sum_up(product_up(product_up(qr2,qr2),sum_up(radial,1.)),
               product_up(product_up(qz2,qz2),radial)));
    if(!interval_finite(quadrature)||!std::isfinite(remainder))return interval_invalid();
    auto result=interval_sum(quadrature,{-remainder,remainder});
    if(interval_finite(result))result.lower=std::max(0.,result.lower);
    return result;
}
/** Three-point tensor Gauss, exact nodes 0,+/-sqrt(3/5), weights
 * 8/9,5/9,5/9. The one-axis remainder is L^7/2016000*max|f^(6)|.
 * The same Newton/Legendre derivative proof gives the reliable tensor
 * remainder area*pi/5600*((dr/d)^6*(1+rh/d)+(dz/d)^6*rh/d).
 * All irrational nodes, rational weights and evaluation are enclosed.
 */
ARCH_HEAVY_INLINE SignedInterval separated_gauss3_integral_enclosure(
    double rl,double rh,double zl,double zh,double ro,double zo,double d_lower,
    std::uint64_t* kernel_count=nullptr,std::uint64_t* agm_count=nullptr) {
    if(!(d_lower>0.))return interval_invalid();
    const auto dr=offset_interval(rh,rl),dz=offset_interval(zh,zl);
    const auto hr=interval_product(dr,{.5,.5}),hz=interval_product(dz,{.5,.5});
    const auto mr=interval_sum({rl,rl},hr),mz=interval_sum({zl,zl},hz);
    const SignedInterval root{positive_down(std::sqrt(positive_down(3./5.))),
                             positive_up(std::sqrt(quotient_up(3.,5.)))};
    const SignedInterval nodes[]{interval_negate(root),{},root};
    const SignedInterval weights[]{
        {positive_down(5./9.),quotient_up(5.,9.)},
        {positive_down(8./9.),quotient_up(8.,9.)},
        {positive_down(5./9.),quotient_up(5.,9.)}};
    SignedInterval sum{};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
        const auto radius=interval_sum(mr,interval_product(hr,nodes[i]));
        const auto axial=interval_sum(mz,interval_product(hz,nodes[j]));
        const auto kernel=separated_kernel_enclosure(radius,axial,ro,zo,kernel_count,agm_count);
        if(!interval_finite(kernel))return interval_invalid();
        sum=interval_sum(sum,interval_product(interval_product(weights[i],weights[j]),kernel));
    }
    const auto area=interval_product(dr,dz);
    const auto quadrature=interval_product(interval_product(area,{.25,.25}),sum);
    const double qr=quotient_up(dr.upper,d_lower),qz=quotient_up(dz.upper,d_lower);
    const double qr2=product_up(qr,qr),qz2=product_up(qz,qz);
    const double qr6=product_up(product_up(qr2,qr2),qr2);
    const double qz6=product_up(product_up(qz2,qz2),qz2);
    const double radial=quotient_up(rh,d_lower);
    const double remainder=product_up(quotient_up(
        product_up(area.upper,positive_up(arch::constants::math::pi)),5600.),
        sum_up(product_up(qr6,sum_up(radial,1.)),product_up(qz6,radial)));
    if(!interval_finite(quadrature)||!std::isfinite(remainder))return interval_invalid();
    auto result=interval_sum(quadrature,{-remainder,remainder});
    if(interval_finite(result))result.lower=std::max(0.,result.lower);
    return result;
}
ARCH_HEAVY_INLINE SignedInterval positive_log_point_enclosure(double x) {
    if(!(x>0.)||!std::isfinite(x))return interval_invalid();
    if(x>=1.)return {logarithm_lower(x),logarithm_upper(x)};
    const auto inverse=interval_quotient_positive({1.,1.},{x,x});
    if(!interval_finite(inverse))return interval_invalid();
    return {-logarithm_upper(inverse.upper),
            -logarithm_lower(std::max(1.,inverse.lower))};
}
ARCH_HEAVY_INLINE SignedInterval positive_log_enclosure(SignedInterval x) {
    if(!interval_finite(x)||!(x.lower>0.))return interval_invalid();
    const auto lo=positive_log_point_enclosure(x.lower),hi=positive_log_point_enclosure(x.upper);
    return {lo.lower,hi.upper};
}
/** atan on [0,1] by exact half angle, then 60-term alternating series.
 * t=x/(1+sqrt(1+x^2))<=sqrt(2)-1; next term bounds the remainder.
 */
ARCH_HEAVY_INLINE SignedInterval atan_unit_point_enclosure(double x) {
    if(!std::isfinite(x)||x<0.||x>1.)return interval_invalid();
    if(x==0.)return {};
    if(x==1.)return {positive_down(.25*arch::constants::math::pi),
                    positive_up(.25*arch::constants::math::pi)};
    const auto hyp=distance_interval(1.,x);
    const auto t=interval_quotient_positive({x,x},
        interval_sum({1.,1.},{hyp.lower,hyp.upper}));
    if(!interval_finite(t)||t.lower<0.)return interval_invalid();
    const auto t2=interval_product(t,t);auto power=t;SignedInterval sum{};
    for(int k=0;k<60;++k) {
        auto term=interval_quotient_positive(power,{2.*k+1.,2.*k+1.});
        if(k%2)term=interval_negate(term);
        sum=interval_sum(sum,term);power=interval_product(power,t2);
    }
    const double tail=quotient_up(power.upper,121.);
    return interval_product({2.,2.},interval_sum(sum,{-tail,tail}));
}
ARCH_HEAVY_INLINE SignedInterval positive_atan_point_enclosure(double x) {
    if(!std::isfinite(x)||x<0.)return interval_invalid();
    if(x<=1.)return atan_unit_point_enclosure(x);
    auto inverse=interval_quotient_positive({1.,1.},{x,x});
    if(!interval_finite(inverse))return interval_invalid();
    inverse.lower=std::max(0.,inverse.lower);inverse.upper=std::min(1.,inverse.upper);
    const auto lo=atan_unit_point_enclosure(inverse.lower),hi=atan_unit_point_enclosure(inverse.upper);
    return interval_sum({positive_down(.5*arch::constants::math::pi),
                         positive_up(.5*arch::constants::math::pi)},
                        interval_negate({lo.lower,hi.upper}));
}
ARCH_HEAVY_INLINE SignedInterval positive_atan_enclosure(SignedInterval x) {
    if(!interval_finite(x)||x.lower<0.)return interval_invalid();
    const auto lo=positive_atan_point_enclosure(x.lower),hi=positive_atan_point_enclosure(x.upper);
    return {lo.lower,hi.upper};
}
/** Integral of log(sqrt(x^2+y^2)) over [0,a] x [0,b].
 * Exact finite primitive; degenerate quadrants have zero measure.
 */
ARCH_HEAVY_INLINE SignedInterval quadrant_log_distance_integral(
    SignedInterval a,SignedInterval b) {
    if(!interval_finite(a)||!interval_finite(b)||a.lower<0.||b.lower<0.)
        return interval_invalid();
    if(a.upper==0.||b.upper==0.)return {};
    if(!(a.lower>0.)||!(b.lower>0.))return interval_invalid();
    const auto lo=distance_interval(a.lower,b.lower),hi=distance_interval(a.upper,b.upper);
    const auto logarithm=positive_log_enclosure({lo.lower,hi.upper});
    const auto ab=interval_product(a,b);
    const auto angle_ba=positive_atan_enclosure(interval_quotient_positive(b,a));
    const auto angle_ab=positive_atan_enclosure(interval_quotient_positive(a,b));
    return interval_sum(interval_product(ab,interval_sum(logarithm,{-1.5,-1.5})),
        interval_product({.5,.5},interval_sum(
            interval_product(interval_product(a,a),angle_ba),
            interval_product(interval_product(b,b),angle_ab))));
}
/** Tight contact bound by analytically integrating the logarithmic main part.
 * NIST DLMF 19.12.1/.3: K(q)=sum c_n q^(2n)[log(1/q)+d_n],
 * c_0=1, 0<c_n<=1, 0<d_n<=log(4). Hence
 * L=log(4/q) <= K(q) <= L/(1-q^2) for 0<q<1.
 * Contact itself has zero measure; the exact log-distance primitive integrates
 * its singularity. No singular sample, epsilon, or quadrature difference.
 */
ARCH_HEAVY_INLINE SignedInterval contact_log_integral_enclosure(
    double rl,double rh,double zl,double zh,double ro,double zo) {
    if(!(rl<=ro&&ro<=rh&&zl<=zo&&zo<=zh))return interval_invalid();
    const auto vertical=absolute_offset_range(zl,zh,zo);
    const auto radial=absolute_offset_range(rl,rh,ro);
    const auto dmax=distance_interval(radial.upper,vertical.upper);
    const auto smin=distance_interval(positive_down(ro+rl),0.);
    const auto smax=distance_interval(sum_up(ro,rh),vertical.upper);
    if(!(smin.lower>0.)||!std::isfinite(smax.upper))return interval_invalid();
    const double q=quotient_up(dmax.upper,smin.lower);
    const double denominator=positive_down(1.-product_up(q,q));
    if(!(denominator>0.))return interval_invalid();
    const auto log_scale=positive_log_enclosure(
        {positive_down(4.*smin.lower),product_up(4.,smax.upper)});
    const SignedInterval xs[]{ro==rl?SignedInterval{}:offset_interval(ro,rl),
                               ro==rh?SignedInterval{}:offset_interval(rh,ro)};
    const SignedInterval ys[]{zo==zl?SignedInterval{}:offset_interval(zo,zl),
                               zo==zh?SignedInterval{}:offset_interval(zh,zo)};
    SignedInterval logarithm_integral{};
    for(const auto a:xs)for(const auto b:ys)if(a.upper>0.&&b.upper>0.) {
        const auto area=interval_product(a,b);
        logarithm_integral=interval_sum(logarithm_integral,
            interval_sum(interval_product(area,log_scale),
                interval_negate(quadrant_log_distance_integral(a,b))));
    }
    if(!interval_finite(logarithm_integral))return interval_invalid();
    logarithm_integral.lower=std::max(0.,logarithm_integral.lower);
    const SignedInterval ratio{positive_down(rl/smax.upper),quotient_up(rh,smin.lower)};
    const auto lower=interval_product(ratio,logarithm_integral);
    const auto upper=interval_quotient_positive(lower,{denominator,denominator});
    if(!interval_finite(lower)||!interval_finite(upper))return interval_invalid();
    return {lower.lower,upper.upper};
}
struct RingBox {
    double rl,rh,zl,zh;
    PositiveInterval integral{};
    RingIntervalStatus status=RingIntervalStatus::InvalidInput;
    std::uint64_t kernel_enclosures=0,agm_iterations=0;
};
/** Integral enclosure of r*K(d/s)/s over one source rectangle.
 * Contact bound: denominator in angular integral >= max(q,2 theta/pi),
 * hence K <= pi/2*(1+log(1/q)). r/s<=1 and s<=S.
 * Each observer-vertex quadrant contributes <=
 * area*pi/2*(2+log(S/max(a,b))); integral log distance is finite.
 */
ARCH_HEAVY_INLINE RingBox enclose_box(double rl,double rh,double zl,double zh,
    double ro,double zo) {
    RingBox result{rl,rh,zl,zh};
    const auto dr=absolute_offset_range(rl,rh,ro);
    const auto dz=absolute_offset_range(zl,zh,zo);
    const auto dmin=distance_interval(dr.lower,dz.lower);
    const auto dmax=distance_interval(dr.upper,dz.upper);
    const double srlo=positive_down(ro+rl),srhi=positive_up(ro+rh);
    const auto smin=distance_interval(srlo,dz.lower);
    const auto smax=distance_interval(srhi,dz.upper);
    if(!std::isfinite(smax.upper) || !(smax.upper>0.)) {
        result.status=RingIntervalStatus::PrecisionLimit;return result;
    }
    const bool contact=rl<=ro && ro<=rh && zl<=zo && zo<=zh;
    if(contact) {
        const double a[]{ro==rl?0.:positive_up(ro-rl),ro==rh?0.:positive_up(rh-ro)};
        const double b[]{zo==zl?0.:positive_up(zo-zl),zo==zh?0.:positive_up(zh-zo)};
        double upper=0.;
        for(double x:a)for(double y:b)if(x>0. && y>0.) {
            const double extent=x>y?x:y;
            const double ratio=quotient_up(smax.upper,extent);
            const double logarithm=logarithm_upper(ratio);
            const double area=product_up(x,y);
            const double factor=product_up(.5,positive_up(arch::constants::math::pi));
            upper=sum_up(upper,product_up(product_up(area,factor),sum_up(2.,logarithm)));
        }
        result.integral={0.,upper};
        const auto logarithm=contact_log_integral_enclosure(rl,rh,zl,zh,ro,zo);
        if(interval_finite(logarithm)) {
            result.integral.lower=std::max(result.integral.lower,logarithm.lower);
            result.integral.upper=std::min(result.integral.upper,logarithm.upper);
        }

    } else {
        if(!(dmin.lower>0.) || !(smin.lower>0.)) {
            result.status=RingIntervalStatus::PrecisionLimit;return result;
        }
        const double qlo=positive_down(dmin.lower/smax.upper);
        const double qhi=std::min(1.,quotient_up(dmax.upper,smin.lower));
        if(!(qlo>0.)) {result.status=RingIntervalStatus::PrecisionLimit;return result;}
        const auto khigh=ring_elliptic_k_interval(qlo);
        const auto klow=ring_elliptic_k_interval(qhi);
        result.kernel_enclosures+=2;
        result.agm_iterations+=khigh.iterations+klow.iterations;
        if(khigh.status!=RingIntervalStatus::Bounded || klow.status!=RingIntervalStatus::Bounded) {
            result.status=RingIntervalStatus::PrecisionLimit;return result;
        }
        const double area_lo=positive_down(positive_down(rh-rl)*positive_down(zh-zl));
        const double area_hi=product_up(positive_up(rh-rl),positive_up(zh-zl));
        const double kernel_lo=positive_down(positive_down(rl/smax.upper)*klow.lower);
        const double kernel_hi=product_up(quotient_up(rh,smin.lower),khigh.upper);
        result.integral={positive_down(area_lo*kernel_lo),product_up(area_hi,kernel_hi)};
        const auto gauss=separated_gauss2_integral_enclosure(
            rl,rh,zl,zh,ro,zo,dmin.lower,
            &result.kernel_enclosures,&result.agm_iterations);
        if(interval_finite(gauss)) {
            result.integral.lower=std::max(result.integral.lower,gauss.lower);
            result.integral.upper=std::min(result.integral.upper,gauss.upper);
        } // Disjoint bounds below fail; never overwrite with a looser fallback.
        const auto gauss3=separated_gauss3_integral_enclosure(
            rl,rh,zl,zh,ro,zo,dmin.lower,
            &result.kernel_enclosures,&result.agm_iterations);
        if(interval_finite(gauss3)) {
            result.integral.lower=std::max(result.integral.lower,gauss3.lower);
            result.integral.upper=std::min(result.integral.upper,gauss3.upper);
        }


    }
    if(!std::isfinite(result.integral.upper) || result.integral.lower>result.integral.upper) {
        result.status=RingIntervalStatus::PrecisionLimit;return result;
    }
    result.status=RingIntervalStatus::Bounded;return result;
}
} // namespace finite_ring_detail

/** Encloses the exact integral for stored geometry/density/G, including contact.
 * Bounds remain diagnostic when a requested target fails; status WorkLimit is
 * not successful convergence. Resource controller is CPU only at this node.
 */
inline RingPotentialEnclosure finite_ring_potential_enclosure(
    double rl,double rh,double zl,double zh,double density,
    double ro,double zo,double G,const RingEnclosureControl& control) {
    using namespace finite_ring_detail;
    RingPotentialEnclosure result{};
    if(!std::isfinite(rl)||!std::isfinite(rh)||rl<0.||!(rl<rh)
        ||!std::isfinite(zl)||!std::isfinite(zh)||!(zl<zh)
        ||!std::isfinite(ro)||ro<0.||!std::isfinite(zo)
        ||!std::isfinite(density)||density<0.||!std::isfinite(G)||!(G>0.)
        ||!std::isfinite(control.relative_target)||control.relative_target<0.
        ||!std::isfinite(control.absolute_target)||control.absolute_target<0.
        ||control.maximum_boxes==0 || control.maximum_boxes>65536)return result;
    if(density==0.) {
        result.lower=result.upper=result.value=result.absolute_error=0.;
        result.status=RingIntervalStatus::Bounded;result.bound_valid=true;return result;
    }
    if(ro==0.)return axis_potential_enclosure(rl,rh,zl,zh,density,zo,G,control);
    // A certified single-leaf far expansion is useful only if the actual
    // requested budget (including FP64 evaluation) passes. Otherwise retain
    // the existing source subdivision/failure path, never a geometric shortcut.
    const auto far=single_leaf_far_potential_enclosure(rl,rh,zl,zh,density,ro,zo,G);
    if(interval_finite(far)) {
        const double value=far.lower+.5*(far.upper-far.lower);
        const double error=positive_up(std::max(value-far.lower,far.upper-value));
        const double target=std::max(control.absolute_target,
            positive_down(control.relative_target*std::abs(value)));
        if(std::isfinite(value)&&std::isfinite(error)&&error<=target) {
            result.lower=far.lower;result.upper=far.upper;result.value=value;
            result.absolute_error=error;result.bound_valid=true;
            result.status=RingIntervalStatus::Bounded;return result;
        }
    }
    const double factor_lo=positive_down(positive_down(4.*G)*density);
    const double factor_hi=product_up(product_up(4.,G),density);
    if(!std::isfinite(factor_hi)) {result.status=RingIntervalStatus::PrecisionLimit;return result;}
    std::vector<RingBox> boxes{enclose_box(rl,rh,zl,zh,ro,zo)};
    result.range_evaluations=1;
    result.kernel_enclosures=boxes[0].kernel_enclosures;
    result.agm_iterations=boxes[0].agm_iterations;
    for(;;) {
        double sum_lo=0.,sum_hi=0.,largest=-1.;std::size_t worst=0;
        for(std::size_t i=0;i<boxes.size();++i) {
            const auto& box=boxes[i];
            if(box.status!=RingIntervalStatus::Bounded) {
                result.status=box.status;result.bound_valid=false;return result;
            }
            if(box.integral.lower!=0.)sum_lo=positive_down(sum_lo+box.integral.lower);
            sum_hi=sum_up(sum_hi,box.integral.upper);
            const double width=box.integral.upper-box.integral.lower;
            if(width>largest) {largest=width;worst=i;}
        }
        result.lower=-product_up(factor_hi,sum_hi);
        result.upper=-positive_down(factor_lo*sum_lo);
        // Same-sign endpoints: this form preserves a representable endpoint
        // when halving a subnormal endpoint would round it outside the interval.
        result.value=result.lower+.5*(result.upper-result.lower);
        result.absolute_error=positive_up(std::max(result.value-result.lower,result.upper-result.value));
        result.leaf_boxes=boxes.size();
        result.bound_valid=std::isfinite(result.lower)&&std::isfinite(result.upper)
            &&std::isfinite(result.value)&&std::isfinite(result.absolute_error);
        if(!result.bound_valid) {result.status=RingIntervalStatus::PrecisionLimit;return result;}
        const double safe_target=std::max(control.absolute_target,
            positive_down(control.relative_target*std::abs(result.value)));
        if(result.absolute_error<=safe_target) {result.status=RingIntervalStatus::Bounded;return result;}
        if(boxes.size()>=control.maximum_boxes) {result.status=RingIntervalStatus::WorkLimit;return result;}
        const auto parent=boxes[worst];
        RingBox first=parent,second=parent;
        if(parent.rh-parent.rl>=parent.zh-parent.zl) {
            const double mid=.5*parent.rl+.5*parent.rh;
            if(!(parent.rl<mid && mid<parent.rh)) {result.status=RingIntervalStatus::PrecisionLimit;return result;}
            first.rh=second.rl=mid;
        } else {
            const double mid=.5*parent.zl+.5*parent.zh;
            if(!(parent.zl<mid && mid<parent.zh)) {result.status=RingIntervalStatus::PrecisionLimit;return result;}
            first.zh=second.zl=mid;
        }
        first=enclose_box(first.rl,first.rh,first.zl,first.zh,ro,zo);
        second=enclose_box(second.rl,second.rh,second.zl,second.zh,ro,zo);
        result.range_evaluations+=2;
        result.kernel_enclosures+=first.kernel_enclosures+second.kernel_enclosures;
        result.agm_iterations+=first.agm_iterations+second.agm_iterations;
        boxes[worst]=first;boxes.push_back(second);
    }
}
} // namespace Physical::Gravity
