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
    std::uint64_t leaf_boxes=0,range_evaluations=0;
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
struct RingBox {
    double rl,rh,zl,zh;
    PositiveInterval integral{};
    RingIntervalStatus status=RingIntervalStatus::InvalidInput;
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
    } else {
        if(!(dmin.lower>0.) || !(smin.lower>0.)) {
            result.status=RingIntervalStatus::PrecisionLimit;return result;
        }
        const double qlo=positive_down(dmin.lower/smax.upper);
        const double qhi=std::min(1.,quotient_up(dmax.upper,smin.lower));
        if(!(qlo>0.)) {result.status=RingIntervalStatus::PrecisionLimit;return result;}
        const auto khigh=ring_elliptic_k_interval(qlo);
        const auto klow=ring_elliptic_k_interval(qhi);
        if(khigh.status!=RingIntervalStatus::Bounded || klow.status!=RingIntervalStatus::Bounded) {
            result.status=RingIntervalStatus::PrecisionLimit;return result;
        }
        const double area_lo=positive_down(positive_down(rh-rl)*positive_down(zh-zl));
        const double area_hi=product_up(positive_up(rh-rl),positive_up(zh-zl));
        const double kernel_lo=positive_down(positive_down(rl/smax.upper)*klow.lower);
        const double kernel_hi=product_up(quotient_up(rh,smin.lower),khigh.upper);
        result.integral={positive_down(area_lo*kernel_lo),product_up(area_hi,kernel_hi)};
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
    const double factor_lo=positive_down(positive_down(4.*G)*density);
    const double factor_hi=product_up(product_up(4.,G),density);
    if(!std::isfinite(factor_hi)) {result.status=RingIntervalStatus::PrecisionLimit;return result;}
    std::vector<RingBox> boxes{enclose_box(rl,rh,zl,zh,ro,zo)};
    result.range_evaluations=1;
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
        boxes[worst]=first;boxes.push_back(second);
    }
}
} // namespace Physical::Gravity
