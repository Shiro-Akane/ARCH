/** @file GravityBoundary.h
 * Isolated Newtonian boundary values from the actual leaf mass distribution.
 * Geometry is cached; moments and their evaluation are shared Host/Device math.
 * Workflow:
 * 1. Receive active density with mesh and generation identity.
 * 2. Define the device-shareable multipole leaves and boundary evaluation work.
 * 3. Reuse bounded leaf/quartet intervals only after current precision/work checks.
 * 4. Publish a checked potential/acceleration field for the requested stage.
 */

#pragma once

#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "core/CompensatedSum.h"
#include "physics/gravity/FiniteRingBoundaryMath.h"
#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/GravitySourceBounds.h"
#include "grid/GridGeometryView.h"
#include "grid/GridMetrics.h"
#include "numerics/elliptic/CompositePoisson.h"

namespace Physical::Gravity {
namespace ring_memo_detail {
/** Exact relative axial endpoints for optional potential-integral memo keys.
 * Workflow: require nearest rounding and safe input exponents -> use one
 * stored-operation TwoSum for each z_endpoint-z_observer -> keep BOTH high and
 * low terms -> compare exact reflected pairs (-upper,-lower) by their bit words
 * -> normalize signed zero only. Equal returned values represent the identical
 * translated/reflected scalar-potential integral; they grant no source, cache,
 * field, force or Runtime authority. Unsupported inputs simply decline key
 * normalization, leaving the caller's original raw-key/kernel path available.
 * Formula: a+b = hi+lo exactly; b_virtual=hi-a,
 * lo=(a-(hi-b_virtual))+(b-b_virtual), with b=-z_observer.
 * Inputs with exponent in [-400,400] keep possible nonzero subtraction
 * residuals normal and avoid relying on subnormal/FTZ behavior. Every elementary
 * operation is separately stored to retain the strict binary64 evaluation.
 */
inline std::optional<std::array<double,4>> exact_axial_relative_endpoints(
    double lower,double upper,double observer) noexcept {
    if(std::fegetround()!=FE_TONEAREST||!(lower<upper))return std::nullopt;
    const auto ordinary=[](double value) noexcept {
        if(!std::isfinite(value))return false;
        if(value==0.)return true;
        const int exponent=std::ilogb(std::abs(value));
        return exponent>=-400&&exponent<=400;
    };
    if(!ordinary(lower)||!ordinary(upper)||!ordinary(observer))return std::nullopt;
    const auto two_sum=[](double a,double b,std::array<double,2>& pair) noexcept {
        volatile double high=a+b;
        volatile double virtual_b=high-a;
        volatile double virtual_a=high-virtual_b;
        volatile double residual_a=a-virtual_a;
        volatile double residual_b=b-virtual_b;
        volatile double low=residual_a+residual_b;
        const std::array<double,6> terms{high,virtual_b,virtual_a,residual_a,residual_b,low};
        for(double value:terms)
            if(!std::isfinite(value)||(value!=0.&&!std::isnormal(value)))return false;
        pair={high==0.?0.:static_cast<double>(high),low==0.?0.:static_cast<double>(low)};
        return true;
    };
    std::array<double,2> lo{},hi{};
    if(!two_sum(lower,-observer,lo)||!two_sum(upper,-observer,hi))return std::nullopt;
    const auto normalize_zero=[](double value) noexcept {return value==0.?0.:value;};
    std::array<double,4> direct{lo[0],lo[1],hi[0],hi[1]};
    const std::array<double,4> reflected{normalize_zero(-hi[0]),normalize_zero(-hi[1]),
        normalize_zero(-lo[0]),normalize_zero(-lo[1])};
    for(std::size_t word=0;word<direct.size();++word) {
        const auto a=std::bit_cast<std::uint64_t>(direct[word]);
        const auto b=std::bit_cast<std::uint64_t>(reflected[word]);
        if(b<a)return reflected;
        if(a<b)return direct;
    }
    return direct;
}
} // namespace ring_memo_detail

struct BoundaryMoments { double value[10]{}; }; // M, dipole[3], symmetric second moment[6].
struct BoundaryTreeNode {
    std::array<double,3> center{};
    std::array<double,3> native_lower{},native_width{};
    double radius_squared=0.;
    BoundaryMoments unit_moments{}; // Unit-density finite-volume moments about center.
    int cell=-1, end=0;
    int children[8]{-1,-1,-1,-1,-1,-1,-1,-1};
};
/** Map a symmetric tensor pair to the compact six-entry second-moment layout. */
ARCH_INLINE int second_moment_index(int a,int b) {
    if(a>b) {const int tmp=a;a=b;b=tmp;}
    return a==0 ? 4+b : a==1 ? 6+b : 9;
}
/** Exact full-ring leaf moments about (0,0,z_mid), no quadrature or point mass. */
ARCH_INLINE BoundaryMoments finite_ring_unit_moments(
    double r_lower,double r_upper,double dz) {
    BoundaryMoments m{};
    m.value[0]=GridMetrics::Rz::CellVolume(r_lower,r_upper,dz);
    m.value[second_moment_index(0,0)]
        =m.value[second_moment_index(1,1)]
        =m.value[0]*(r_lower*r_lower+r_upper*r_upper)/4.;
    m.value[second_moment_index(2,2)]=m.value[0]*dz*dz/12.;
    // Axial midpoint and a complete azimuth guarantee zero dipole/cross moments.
    return m;
}
/** Full physical support about the ring-axis center, not a meridional half diagonal. */
ARCH_INLINE double finite_ring_support_squared(double outer,double axial_half_width) {
    return outer*outer+axial_half_width*axial_half_width;
}
enum class MultipoleBoundStatus : unsigned char { Bounded, InvalidInput, NotSeparated, Overflow };
struct MultipoleTruncationBound {
    double value=std::numeric_limits<double>::infinity();
    MultipoleBoundStatus status=MultipoleBoundStatus::InvalidInput;
};
/**
 * Legendre remainder after quadrupole. absolute_mass is integral |rho| dV,
 * never abs(net mass) for signed manufactured data. Inversion symmetry is a
 * proven source property (full-ring uniform leaf), not inferred for a parent.
 * This covers truncation only; distance/support and accumulated arithmetic
 * rounding still belong to the eventual boundary error ledger.
 */
ARCH_INLINE MultipoleTruncationBound multipole_truncation_bound(
    double G,double absolute_mass,double support,double distance,
    bool inversion_symmetric) {
    MultipoleTruncationBound result{};
    if(!std::isfinite(G) || G<=0. || !std::isfinite(absolute_mass)
        || absolute_mass<0. || !std::isfinite(support) || support<0.
        || !std::isfinite(distance) || distance<=0.)return result;
    if(distance<=support) {result.status=MultipoleBoundStatus::NotSeparated;return result;}
    if(absolute_mass==0. || support==0.) {
        result.value=0.;result.status=MultipoleBoundStatus::Bounded;return result;
    }
    // Round the positive scalar series upward, without global rounding-mode
    // mutation. Failure/overflow never becomes a zero-error witness.
    const double infinity=std::numeric_limits<double>::infinity();
    const auto up=[&](double v) {return std::nextafter(v,infinity);};
    const double q=up(support/distance);
    if(q>=1.) {result.status=MultipoleBoundStatus::NotSeparated;return result;}
    const double q2=up(q*q);
    const double power=inversion_symmetric?up(q2*q2):up(q2*q);
    const double denominator=std::nextafter(
        inversion_symmetric?1.-q2:1.-q,0.);
    if(!(denominator>0.)) {result.status=MultipoleBoundStatus::NotSeparated;return result;}
    const double amplitude=up(G*up(absolute_mass/distance));
    result.value=up(up(amplitude*power)/denominator);
    result.status=std::isfinite(result.value)
        ? MultipoleBoundStatus::Bounded:MultipoleBoundStatus::Overflow;
    return result;
}
/** Shared Newtonian moments; ring and legacy tree consumers use the same ten slots. */
ARCH_INLINE double newtonian_multipole_potential(
    const BoundaryMoments& m,const double* r,double r2,double G,int order) {
    double term=m.value[0];
    if(order>=1)for(int a=0;a<3;++a)term+=m.value[1+a]*r[a]/r2;
    if(order>=2) {
        double contraction=0.,trace=0.;
        for(int a=0;a<3;++a) {
            trace+=m.value[second_moment_index(a,a)];
            for(int b=0;b<3;++b)contraction+=r[a]*r[b]*m.value[second_moment_index(a,b)];
        }
        term+=(3.*contraction/r2-trace)/(2.*r2);
    }
    const double inverse=1./std::sqrt(r2);
    return -G*inverse*term;
}
/** Translate and sum child moments about the parent expansion center. */
ARCH_INLINE BoundaryMoments combine_boundary_moments(const BoundaryTreeNode* nodes,
    const BoundaryMoments* moments,int index) {
    arch::math::CompensatedSum sum[10];
    const auto& node=nodes[index];
    for(int child:node.children) if(child>=0) {
        const auto& m=moments[child];
        double delta[3];
        for(int a=0;a<3;++a) delta[a]=nodes[child].center[a]-node.center[a];
        sum[0].add(m.value[0]);
        for(int a=0;a<3;++a) sum[1+a].add(m.value[1+a]+delta[a]*m.value[0]);
        for(int a=0;a<3;++a) for(int b=a;b<3;++b) {
            const int q=second_moment_index(a,b);
            // Q_ab(parent) = Q_ab(child) + d_a D_b + d_b D_a + d_a d_b M.
            sum[q].add(m.value[q]+delta[a]*m.value[1+b]+delta[b]*m.value[1+a]
                       +delta[a]*delta[b]*m.value[0]);
        }
    }
    BoundaryMoments result;
    for(int q=0;q<10;++q) result.value[q]=sum[q].value();
    return result;
}
/** Instantaneous error companion for the existing ten moments, not a second
 * evolved source/cache. Geometry bounds describe the exact stored ring edges.
 * Workflow: enclose a finite leaf -> translate/sum using the original formula
 * -> accept a separated node only when its full interval fits the face budget.
 */
struct RingMomentEnclosure {
    finite_ring_detail::SignedInterval value[10]{};
    double support_upper=0.;
    std::uint64_t leaves=0;
    bool valid=false;
};
ARCH_HEAVY_INLINE RingMomentEnclosure finite_ring_moment_enclosure(
    double rl,double rh,double zl,double zh,double density,
    const std::array<double,3>& center) {
    using namespace finite_ring_detail;
    RingMomentEnclosure result;
    if(!std::isfinite(rl)||!std::isfinite(rh)||rl<0.||rh<=rl
        ||!std::isfinite(zl)||!std::isfinite(zh)||zh<=zl
        ||!std::isfinite(density)||density<0.||center[0]!=0.||center[1]!=0.
        ||!std::isfinite(center[2]))return result;
    const auto dr=offset_interval(rh,rl),dz=offset_interval(zh,zl);
    const auto middle=interval_sum({zl,zl},interval_product(dz,{.5,.5}));
    const auto delta=interval_sum(middle,interval_negate({center[2],center[2]}));
    const auto pi_range=SignedInterval{positive_down(arch::constants::math::pi),
        positive_up(arch::constants::math::pi)};
    const auto mass=interval_product(interval_product(interval_product(
        interval_product(pi_range,{density,density}),dr),
        interval_sum({rh,rh},{rl,rl})),dz);
    result.value[0]=mass;
    result.value[3]=interval_product(mass,delta);
    const auto radial=interval_sum(interval_product({rl,rl},{rl,rl}),
        interval_product({rh,rh},{rh,rh}));
    result.value[4]=result.value[7]=interval_product(
        interval_product(mass,radial),{.25,.25});
    result.value[9]=interval_product(mass,interval_sum(
        interval_quotient_positive(interval_product(dz,dz),{12.,12.}),
        interval_product(delta,delta)));
    const auto lower=offset_interval(zl,center[2]),upper=offset_interval(zh,center[2]);
    const double extent=std::max({std::abs(lower.lower),std::abs(lower.upper),
        std::abs(upper.lower),std::abs(upper.upper)});
    result.support_upper=distance_interval(rh,extent).upper;
    result.leaves=1;
    result.valid=std::isfinite(result.support_upper)&&mass.lower>=0.;
    for(auto v:result.value)result.valid=result.valid&&interval_finite(v);
    return result;
}
/** Same raw second-moment translation as combine_boundary_moments, enclosed.
 * General parents are not assumed inversion-symmetric; even zero dipole is
 * insufficient proof of vanishing third and higher odd moments.
 */
ARCH_HEAVY_INLINE RingMomentEnclosure combine_ring_moment_enclosures(
    const BoundaryTreeNode* nodes,const RingMomentEnclosure* moments,int index) {
    using namespace finite_ring_detail;
    RingMomentEnclosure result;result.valid=true;
    const auto& node=nodes[index];
    if(node.center[0]!=0.||node.center[1]!=0.||!std::isfinite(node.center[2]))
        {result.valid=false;return result;}
    for(int child:node.children)if(child>=0) {
        const auto& m=moments[child];
        if(!m.valid||nodes[child].center[0]!=0.||nodes[child].center[1]!=0.
            ||!std::isfinite(nodes[child].center[2])){result.valid=false;return result;}
        SignedInterval delta[3];
        for(int a=0;a<3;++a)delta[a]=offset_interval(nodes[child].center[a],node.center[a]);
        result.value[0]=interval_sum(result.value[0],m.value[0]);
        for(int a=0;a<3;++a) {
            result.value[1+a]=interval_sum(result.value[1+a],interval_sum(
                m.value[1+a],interval_product(delta[a],m.value[0])));
            for(int b=a;b<3;++b) {
                const int q=second_moment_index(a,b);
                auto term=interval_sum(m.value[q],interval_product(delta[a],m.value[1+b]));
                term=interval_sum(term,interval_product(delta[b],m.value[1+a]));
                term=interval_sum(term,interval_product(interval_product(delta[a],delta[b]),m.value[0]));
                result.value[q]=interval_sum(result.value[q],term);
            }
        }
        const double offset=std::max(std::abs(delta[2].lower),std::abs(delta[2].upper));
        result.support_upper=std::max(result.support_upper,sum_up(m.support_upper,offset));
        result.leaves+=m.leaves;
    }
    result.valid=result.leaves>0&&std::isfinite(result.support_upper)&&result.value[0].lower>=0.;
    for(auto v:result.value)result.valid=result.valid&&interval_finite(v);
    return result;
}
/** General nonnegative full-ring node: monopole/dipole/quadrupole and q^3/(1-q).
 * Stored expansion centers, enclosed true leaf moments, support/distance,
 * evaluation and final tail are all included; no symmetry flag is exposed.
 */
ARCH_HEAVY_INLINE finite_ring_detail::SignedInterval ring_node_far_enclosure(
    const BoundaryTreeNode& node,const RingMomentEnclosure& moments,
    double ro,double zo,double G,double* truncation_upper=nullptr,
    finite_ring_detail::SignedInterval* evaluation_interval=nullptr) {
    using namespace finite_ring_detail;
    if(truncation_upper)*truncation_upper=std::numeric_limits<double>::infinity();
    if(evaluation_interval)*evaluation_interval=interval_invalid();
    if(!moments.valid||node.center[0]!=0.||node.center[1]!=0.||!std::isfinite(node.center[2])
        ||!std::isfinite(ro)||ro<0.||!std::isfinite(zo)
        ||!std::isfinite(G)||G<=0.||moments.value[0].lower<0.)return interval_invalid();
    for(auto v:moments.value)if(!interval_finite(v))return interval_invalid();
    if(moments.value[0].lower==0.&&moments.value[0].upper==0.) {
        if(truncation_upper)*truncation_upper=0.;
        if(evaluation_interval)*evaluation_interval={};
        return {};
    }
    const auto axial=offset_interval(zo,node.center[2]);
    const auto distance=axis_hypot_interval(ro,axial);
    if(!interval_finite(distance)||distance.lower<=moments.support_upper)return interval_invalid();
    const double q=quotient_up(moments.support_upper,distance.lower);
    const double denominator=positive_down(1.-q);
    if(!(denominator>0.))return interval_invalid();
    const auto inverse=interval_quotient_positive({1.,1.},distance);
    const SignedInterval direction[3]{interval_product({ro,ro},inverse),{},interval_product(axial,inverse)};
    SignedInterval dipole{},contraction{},trace{};
    for(int a=0;a<3;++a) {
        dipole=interval_sum(dipole,interval_product(moments.value[1+a],direction[a]));
        trace=interval_sum(trace,moments.value[second_moment_index(a,a)]);
        for(int b=0;b<3;++b)contraction=interval_sum(contraction,
            interval_product(moments.value[second_moment_index(a,b)],
                interval_product(direction[a],direction[b])));
    }
    const auto inv2=interval_product(inverse,inverse);
    auto term=interval_sum(interval_product(moments.value[0],inverse),interval_product(dipole,inv2));
    term=interval_sum(term,interval_product(interval_product(
        interval_sum(interval_product({3.,3.},contraction),interval_negate(trace)),{.5,.5}),
        interval_product(inv2,inverse)));
    const auto truncated=interval_negate(interval_product({G,G},term));
    const double leading=product_up(G,quotient_up(moments.value[0].upper,distance.lower));
    const double tail=product_up(leading,quotient_up(product_up(product_up(q,q),q),denominator));
    if(!std::isfinite(tail))return interval_invalid();
    auto result=interval_sum(truncated,{-tail,tail});
    if(interval_finite(result)) {
        result.upper=std::min(0.,result.upper);
        if(truncation_upper)*truncation_upper=tail;
        if(evaluation_interval)*evaluation_interval=truncated;
    }
    return result;
}

/** Integrate only a near-field curved leaf, avoiding a singular point-mass approximation. */
ARCH_INLINE double near_leaf_potential(const BoundaryTreeNode& node,double mass,
    const double* point,double G,double reference_radius,
    GridMetrics::Geometry geometry,int dimension) {
    constexpr double abscissa[]{-0.7745966692414834,0.,0.7745966692414834};
    constexpr double weights[]{5./9.,8./9.,5./9.};
    arch::math::CompensatedSum numerator,denominator;
    for(int u=0;u<3;++u)for(int v=0;v<3;++v)
        for(int w=0;w<(dimension==3?3:1);++w) {
            std::array<double,3> native=node.native_lower;
            native[0]+=0.5*node.native_width[0]*(1.+abscissa[u]);
            native[1]+=0.5*node.native_width[1]*(1.+abscissa[v]);
            if(dimension==3)native[2]+=0.5*node.native_width[2]*(1.+abscissa[w]);
            const auto x=GridMetrics::PhysicalPosition(geometry,dimension,native);
            double r2=0.;for(int a=0;a<3;++a)r2+=(point[a]-x[a])*(point[a]-x[a]);
            double jacobian=native[0];
            if(geometry==GridMetrics::Geometry::Spherical && dimension==3)
                jacobian*=native[0]*std::sin(native[1]);
            const double weight=weights[u]*weights[v]*(dimension==3?weights[w]:1.)*jacobian;
            denominator.add(weight);
            numerator.add(weight*(dimension==2
                ?2.*G*std::log(std::sqrt(r2)/reference_radius)
                :-G/std::sqrt(r2)));
        }
    return mass*numerator.value()/denominator.value();
}
// Threaded depth-first tree: end skips a subtree without a device stack.
// theta/order are internal verification controls, not simulation input parameters.
/** Evaluate the Newtonian multipole expansion with bounded tree opening. */
ARCH_INLINE double isolated_potential(const BoundaryTreeNode* nodes,
    const BoundaryMoments* moments,int count,const double* point,double G,
    double theta=0.25,int order=2,
    GridMetrics::Geometry geometry=GridMetrics::Geometry::Cartesian,int dimension=3) {
    arch::math::CompensatedSum potential;
    for(int index=0;index<count;) {
        const auto& node=nodes[index];
        double r[3],r2=0.;
        for(int a=0;a<3;++a) {r[a]=point[a]-node.center[a];r2+=r[a]*r[a];}
        if(node.cell<0 && node.radius_squared>=theta*theta*r2) {++index;continue;}
        const auto& m=moments[index];
        if(node.cell>=0 && geometry!=GridMetrics::Geometry::Cartesian &&
            node.radius_squared>=theta*theta*r2) {
            potential.add(near_leaf_potential(node,m.value[0],point,G,1.,geometry,dimension));
            index=node.end;continue;
        }
        potential.add(newtonian_multipole_potential(m,r,r2,G,order));
        index=node.end;
    }
    return potential.value();
}
/** Evaluate the two-dimensional infinite-column Green kernel at a face. */
ARCH_INLINE double isolated_log_potential(const BoundaryTreeNode* nodes,
    const BoundaryMoments* moments,int count,const double* point,double G,
    double reference_radius,double theta=0.25,int order=2,
    GridMetrics::Geometry geometry=GridMetrics::Geometry::Cylindrical) {
    arch::math::CompensatedSum potential;
    for(int index=0;index<count;) {
        const auto& node=nodes[index];
        const double x=point[0]-node.center[0],y=point[1]-node.center[1];
        const double r2=x*x+y*y;
        if(node.cell<0 && node.radius_squared>=theta*theta*r2) {++index;continue;}
        const auto& m=moments[index];
        if(node.cell>=0 && node.radius_squared>=theta*theta*r2) {
            potential.add(near_leaf_potential(node,m.value[0],point,G,reference_radius,geometry,2));
            index=node.end;continue;
        }
        double value=m.value[0]*std::log(std::sqrt(r2)/reference_radius);
        if(order>=1)value-=(m.value[1]*x+m.value[2]*y)/r2;
        if(order>=2) {
            const double trace=m.value[second_moment_index(0,0)]
                +m.value[second_moment_index(1,1)];
            const double contraction=x*x*m.value[second_moment_index(0,0)]
                +2.*x*y*m.value[second_moment_index(0,1)]
                +y*y*m.value[second_moment_index(1,1)];
            value+=0.5*trace/r2-contraction/(r2*r2);
        }
        // Phi=2G [M log(R/Rref)-D.R/R^2+tr(Q)/(2R^2)-R.Q.R/R^4].
        potential.add(2.*G*value);
        index=node.end;
    }
    return potential.value();
}
/** Internal source integration budget; never config/API/browser controls.
 * A geometric opening is not a certificate. General node acceptance requires
 * enclosed moments/evaluation/tail fitting a source-count share of the budget;
 * otherwise descend to the original finite-leaf source integration.
 */
struct RingBoundaryControl {
    double face_absolute_target=0.;
    // The existing global work cap includes leaf integration and parent attempts.
    std::uint64_t maximum_boxes_per_leaf=1024,maximum_leaf_evaluations=100000;
};
/** An initial work allocation only. Final RHS/residual assessment is mandatory;
 * no Proposed result qualifies a solved field, continuous physics or publication.
 */
enum class RingBudgetBasis { SourceScale, PositiveIsolatedRhs };
enum class RingBudgetStatus { Proposed, ZeroBudget, InvalidInput, UncertifiedInput, Overflow };
struct RingBoundaryBudgetProposal {
    RingBudgetStatus status=RingBudgetStatus::InvalidInput;
    GravitySolveIdentity source;
    std::uint64_t source_generation=0;
    RingBoundaryControl control;
    double source_norm_lower=0.,boundary_sensitivity_upper=0.,initial_tolerance=0.;
    RingBudgetBasis basis=RingBudgetBasis::SourceScale;
    double rhs_norm_lower=0.,mass_lower=0.,maximum_distance_upper=0.,
        potential_magnitude_lower=0.;
    std::vector<double> rhs_cell_magnitude_lower;

};
enum class RingBoundaryStatus : unsigned char { Bounded, WorkLimit, PrecisionLimit };
/** Plain completion/counter storage borrowed by the shared controller.
 * Full source identity stays with the authenticated Host owner; generation is
 * echoed with these mathematical diagnostics, never a source/field capability.
 */
struct RingBoundaryScalars {
    RingBoundaryStatus status=RingBoundaryStatus::PrecisionLimit;
    std::uint64_t source_generation=0,leaf_evaluations=0,range_evaluations=0,
        kernel_enclosures=0,agm_iterations=0,parent_evaluations=0,parent_acceptances=0,
        represented_leaf_evaluations=0,coalesced_parent_attempts=0,
        coalesced_parent_acceptances=0,coalesced_native_leaves=0;
    // Integral memo diagnostics; tree visits and all source coverage are still charged.
    std::uint64_t memo_hits=0,memo_misses=0,memo_admissions=0;
};
struct RingBoundaryEvaluation : RingBoundaryScalars {
    GravitySolveIdentity source;
    std::vector<double> values,lower,upper,far_truncation_upper,far_evaluation_width_upper;
    std::vector<arch::elliptic::BoundaryPotentialError> errors;
};
namespace ring_boundary_detail {
/** Original operator-owned edge words, never lower+width reconstruction. */
struct RingLeafEdges { double rl=0.,rh=0.,zl=0.,zh=0.; };
/** Actual full face-index order; native (r,z) is not Cartesian (r,0,z). */
struct RingBoundaryFace { double r=0.,z=0.;int boundary_side=-1; };
struct UniformRingQuartet { double rl=0.,rh=0.,zl=0.,zh=0.,density=0.;bool valid=false; };
/** Plain borrowed reads in the executing memory space. The owner authenticates
 * topology/source/geometry first and keeps every node-sized/cell-sized array
 * live for the call. These records grant no source or Runtime authority.
 */
struct RingBoundaryReadView {
    const BoundaryTreeNode* nodes=nullptr;int node_count=0;
    const RingMomentEnclosure* moment_bounds=nullptr;
    const RingLeafEdges* edges=nullptr;const double* density=nullptr;int cell_count=0;
    const RingBoundaryFace* faces=nullptr;std::size_t face_count=0;
    const UniformRingQuartet* quartets=nullptr;
    std::uint64_t source_generation=0;double gravitational_constant=0.;
};
/** Authenticated synchronous Host packet, never a device work descriptor.
 * Transient geometry/evidence vectors own their storage; actual source nodes
 * and density remain borrowed from GravityBoundary until the call completes.
 * view() rebuilds every vector pointer after moves/copies, retaining exact edge
 * words and original full face indices. The copied identity stays Host-only.
 */
struct PreparedRingBoundaryInputs {
    GravitySolveIdentity source;RingBoundaryControl control;
    const BoundaryTreeNode* nodes=nullptr;int node_count=0;
    const double* density=nullptr;int cell_count=0;std::uint64_t source_generation=0;
    std::vector<RingMomentEnclosure> moment_bounds;
    std::vector<RingLeafEdges> edges;
    std::vector<RingBoundaryFace> faces;
    std::vector<UniformRingQuartet> quartets;
    std::vector<int> surface_faces;
    RingBoundaryReadView view() const noexcept {
        return {nodes,node_count,moment_bounds.data(),edges.data(),density,cell_count,
            faces.data(),faces.size(),quartets.data(),source_generation,source.gravitational_constant};
    }
};
/** Borrowed complete face arrays, including unchanged zero interior records.
 * The owner supplies valid capacities and nonoverlap with reads/leaf scratch. */
struct RingBoundaryOutputView {
    double *values=nullptr,*lower=nullptr,*upper=nullptr;
    double *far_truncation_upper=nullptr,*far_evaluation_width_upper=nullptr;
    arch::elliptic::BoundaryPotentialError* errors=nullptr;std::size_t face_count=0;
};
/** Exact same-density rectangle union, never a bbox/mass approximation.
 * Direct four leaf siblings must tile [rl,rh]x[zl,zh] by equal shared edges.
 * Every stored edge is compared exactly. The original finite-ring integral
 * over this stored union equals its four disjoint source integrals; ideal-root
 * potential qualification remains a separate source/observer error contract.
 */
ARCH_HEAVY_INLINE UniformRingQuartet uniform_ring_quartet(
    const RingBoundaryReadView& input,int index) {
    if(index<0||index>=input.node_count||!input.nodes||!input.edges||!input.density)return {};
    const auto& parent=input.nodes[index];
    double edges[4][4]{};
    double rho=0.;
    for(int c=0;c<4;++c) {
        const int child=parent.children[c];
        if(child<0||child>=input.node_count||input.nodes[child].cell<0)return {};
        const int cell=input.nodes[child].cell;
        if(cell>=input.cell_count)return {};
        if(c==0)rho=input.density[cell];
        if(!std::isfinite(rho)||rho<0.||input.density[cell]!=rho)return {};
        const auto& edge=input.edges[cell];
        edges[c][0]=edge.rl;edges[c][1]=edge.rh;edges[c][2]=edge.zl;edges[c][3]=edge.zh;
    }
    for(int c=4;c<8;++c)if(parent.children[c]>=0)return {};
    const double rl=edges[0][0],rm=edges[0][1],rh=edges[3][1];
    const double zl=edges[0][2],zm=edges[0][3],zh=edges[3][3];
    if(!(rl<rm&&rm<rh&&zl<zm&&zm<zh))return {};
    for(int c=0;c<4;++c) {
        const double expected[]{c&1?rm:rl,c&1?rh:rm,c&2?zm:zl,c&2?zh:zm};
        for(int word=0;word<4;++word)if(edges[c][word]!=expected[word])return {};
    }
    return {rl,rh,zl,zh,rho,true};
}
/** Explicit cold numerical history for a future Device owner. Each attempt
 * counts as a memo miss and calls the accepted raw leaf with reusable scratch.
 * There is no second physical approximation or change to the Host memo path.
 */
struct ColdRingLeafEvaluator {
    finite_ring_detail::RingBox* boxes=nullptr;std::size_t box_count=0;
    finite_ring_detail::RingBoxReductionView::Node* nodes=nullptr;std::size_t node_count=0;
    ARCH_HEAVY_INLINE RingPotentialEnclosure operator()(const RingLeafEdges& edge,
        double density,double ro,double zo,double G,const RingEnclosureControl& control,
        bool /*quartet*/,RingBoundaryScalars& diagnostics) const {
        ++diagnostics.memo_misses;
        return finite_ring_detail::finite_ring_potential_enclosure_raw(
            edge.rl,edge.rh,edge.zl,edge.zh,density,ro,zo,G,control,
            boxes,box_count,nodes,node_count);
    }
};
/** One deterministic GLOBAL controller for the authenticated borrowed source.
 * Workflow: retire every output -> actual face-index/DFS order -> charge before
 * parent/exact-quartet/fallback/leaf evaluation -> outward sums -> certify all
 * faces only after full completion. A single invocation owns the whole cap;
 * independently dispatched faces or atomic cap admission change this contract.
 * Callable leaves retain the accepted adaptive max-width/earliest-index body.
 */
template<class LeafEvaluator>
ARCH_HEAVY_INLINE void ring_boundary_shared(const RingBoundaryReadView& input,
    const RingBoundaryControl& control,const RingBoundaryOutputView& output,
    RingBoundaryScalars& result,LeafEvaluator& evaluate_leaf) {
    using namespace finite_ring_detail;
    result=RingBoundaryScalars{};result.source_generation=input.source_generation;
    if(output.errors)for(std::size_t face=0;face<output.face_count;++face)output.errors[face]={};
    if(output.face_count!=input.face_count||!output.values||!output.lower||!output.upper
        ||!output.far_truncation_upper||!output.far_evaluation_width_upper||!output.errors)return;
    for(std::size_t face=0;face<output.face_count;++face) {
        output.values[face]=output.lower[face]=output.upper[face]=0.;
        output.far_truncation_upper[face]=output.far_evaluation_width_upper[face]=0.;
    }
    if(input.node_count<=0||input.cell_count<=0||!input.nodes||!input.moment_bounds
        ||!input.edges||!input.density||!input.faces||!input.quartets
        ||!std::isfinite(input.gravitational_constant)||input.gravitational_constant<=0.
        ||!std::isfinite(control.face_absolute_target)||control.face_absolute_target<0.
        ||control.maximum_boxes_per_leaf==0||control.maximum_boxes_per_leaf>65536
        ||control.maximum_leaf_evaluations==0)return;
    RingEnclosureControl leaf_control{};
    leaf_control.absolute_target=positive_down(control.face_absolute_target/input.cell_count);
    leaf_control.maximum_boxes=control.maximum_boxes_per_leaf;
    bool converged=true;result.status=RingBoundaryStatus::Bounded;
    for(std::size_t face=0;face<input.face_count;++face)if(input.faces[face].boundary_side>=0) {
        SignedInterval total{};
        for(int index=0;index<input.node_count;) {
            const auto& node=input.nodes[index];
            if(result.leaf_evaluations+result.parent_evaluations>=control.maximum_leaf_evaluations) {
                result.status=RingBoundaryStatus::WorkLimit;return;
            }
            if(node.cell<0) {
                ++result.parent_evaluations;
                if(input.quartets[index].valid) {
                    // The parent visit pays for this exact integral first.
                    // Avoid evaluating a multipole approximation unnecessarily.
                    ++result.coalesced_parent_attempts;
                    const auto& tile=input.quartets[index];
                    auto tile_control=leaf_control;
                    tile_control.absolute_target=positive_down(4.*leaf_control.absolute_target);
                    const auto enclosure=evaluate_leaf({tile.rl,tile.rh,tile.zl,tile.zh},
                        tile.density,input.faces[face].r,input.faces[face].z,
                        input.gravitational_constant,tile_control,true,result);
                    result.range_evaluations+=enclosure.range_evaluations;
                    result.kernel_enclosures+=enclosure.kernel_enclosures;
                    result.agm_iterations+=enclosure.agm_iterations;
                    if(enclosure.bound_valid&&enclosure.status==RingIntervalStatus::Bounded) {
                        total=interval_sum(total,{enclosure.lower,enclosure.upper});
                        ++result.coalesced_parent_acceptances;
                        result.coalesced_native_leaves+=4;
                        result.represented_leaf_evaluations+=4;
                        index=node.end;continue;
                    }
                    // Failed exact integral falls back to the old far/descent
                    // path, with its extra evaluation charged to the same cap.
                    if(result.leaf_evaluations+result.parent_evaluations>=control.maximum_leaf_evaluations) {
                        result.status=RingBoundaryStatus::WorkLimit;return;
                    }
                    ++result.parent_evaluations;
                }
                double tail=0.;SignedInterval evaluation{};
                const auto far=ring_node_far_enclosure(node,input.moment_bounds[index],
                    input.faces[face].r,input.faces[face].z,input.gravitational_constant,
                    &tail,&evaluation);
                const double allowance=positive_down(leaf_control.absolute_target*input.moment_bounds[index].leaves);
                const double halfwidth=interval_finite(far)
                    ? positive_up(.5*(far.upper-far.lower)) : std::numeric_limits<double>::infinity();
                if(interval_finite(far)&&halfwidth<=allowance) {
                    total=interval_sum(total,far);
                    ++result.parent_acceptances;
                    output.far_truncation_upper[face]=sum_up(output.far_truncation_upper[face],tail);
                    output.far_evaluation_width_upper[face]=sum_up(output.far_evaluation_width_upper[face],
                        positive_up(evaluation.upper-evaluation.lower));
                    result.represented_leaf_evaluations+=input.moment_bounds[index].leaves;
                    index=node.end;continue;
                }
                ++index;continue; // Budget/separation failure descends; no geometric-only opening.
            }
            ++result.leaf_evaluations;
            ++result.represented_leaf_evaluations;
            ++index;
            // Same operator-owned edge arithmetic used by unit_cell_moments;
            // lower+width could re-round an upper edge differently.
            const auto leaf=evaluate_leaf(input.edges[node.cell],
                input.density[node.cell],input.faces[face].r,
                input.faces[face].z,input.gravitational_constant,leaf_control,false,result);
            result.range_evaluations+=leaf.range_evaluations;
            result.kernel_enclosures+=leaf.kernel_enclosures;
            result.agm_iterations+=leaf.agm_iterations;
            if(!leaf.bound_valid) {
                result.status=RingBoundaryStatus::PrecisionLimit;return;
            }
            if(leaf.status!=RingIntervalStatus::Bounded) {
                converged=false;
                result.status=leaf.status==RingIntervalStatus::WorkLimit?
                    RingBoundaryStatus::WorkLimit:RingBoundaryStatus::PrecisionLimit;
            }
            total=interval_sum(total,{leaf.lower,leaf.upper});
        }
        if(!interval_finite(total)) {
            result.status=RingBoundaryStatus::PrecisionLimit;return;
        }
        output.lower[face]=total.lower;output.upper[face]=total.upper;
        output.values[face]=total.lower+.5*(total.upper-total.lower);
        const double error=positive_up(std::max(output.values[face]-total.lower,
                                                total.upper-output.values[face]));
        output.errors[face].absolute_error=error;
        if(!std::isfinite(output.values[face])||!std::isfinite(error)) {
            result.status=RingBoundaryStatus::PrecisionLimit;return;
        }
        if(error>control.face_absolute_target) {
            converged=false;if(result.status==RingBoundaryStatus::Bounded)
                result.status=RingBoundaryStatus::PrecisionLimit;
        }
    }
    // Certification is all-or-nothing, including FP64 source reduction/budget.
    if(converged)for(std::size_t face=0;face<output.face_count;++face)
        output.errors[face].quality=arch::elliptic::BoundaryErrorQuality::CertifiedAbsolute;
    return;
}
} // namespace ring_boundary_detail

/** Composed mathematical-source error, conditional on the stored operator.
 * Native geometry/stencil/weights construction remains a mandatory separate
 * physical certificate. No API can upgrade this result into production RZ.
 */
enum class RingRhsAssessmentScope { StoredNativeOperator, RootDyadicNativeOperator };
struct RingRhsAssessment {
    GravitySolveIdentity source;
    std::uint64_t source_generation=0;
    RingRhsAssessmentScope scope=RingRhsAssessmentScope::StoredNativeOperator;
    GravitySourceBounds source_error;
    arch::elliptic::BoundaryRhsError boundary_error,combined_rhs_error;
    arch::elliptic::PoissonArithmeticError assembly_error,residual_error;
    arch::elliptic::NativeRzBoundaryConstructionError native_boundary_construction;
    arch::elliptic::NativeRzBoundaryPotentialError native_boundary_potential;
    arch::elliptic::NativeRzResidualEvaluationError native_residual_error;
    // Complete prescribed residual: joint A/B construction, all other errors once.
    arch::elliptic::NativeRzCompleteResidualError native_complete_residual_error;
    arch::elliptic::BoundaryResidualAssessment conditional;
    arch::elliptic::BoundaryResidualStatus physical_status=
        arch::elliptic::BoundaryResidualStatus::UncertifiedInput;
};
class GravityBoundary {
public:
    explicit GravityBoundary(const arch::elliptic::CompositePoisson&,
        amr::TopologyEpoch bound_topology={});
    void update(std::span<const double> density);
    // Internal checked source-cache path, not production RZ capability.
    void update(std::span<const double> density,const GravitySolveIdentity&);
    std::vector<RingMomentEnclosure> ring_moment_enclosures(
        const arch::elliptic::CompositePoisson&,const GravitySolveIdentity&) const;
    RingBoundaryBudgetProposal propose_ring_budget(
        const arch::elliptic::CompositePoisson&,const GravitySolveIdentity&,
        std::span<const double> computed_source,double rtol,double atol) const;
    /** Bound all tree visits/parent fallback attempts over actual exterior faces.
     * This internal resource policy does not bound adaptive quadrature work,
     * change its finite per-leaf cap, or certify any mathematical error.
     */
    std::uint64_t full_ring_traversal_work_bound(
        const arch::elliptic::CompositePoisson&) const;
    /** Identity-checked actual update generation; no ring/field success grant. */
    std::uint64_t materialized_ring_source_generation(
        const arch::elliptic::CompositePoisson&,const GravitySolveIdentity&) const;
    /** Internal Host packet for either synchronous execution provider.
     * Original source/operator/budget guards run before any Device work; borrowed
     * source reads grant no Runtime capability or completed-field qualification.
     */
    ring_boundary_detail::PreparedRingBoundaryInputs prepare_ring_boundary_inputs(
        const arch::elliptic::CompositePoisson&,const GravitySolveIdentity&,
        const RingBoundaryControl&) const;
    RingBoundaryEvaluation ring_boundary(const arch::elliptic::CompositePoisson&,
        const GravitySolveIdentity&,const RingBoundaryControl&) const;
    /** Discard only instance-owned numeric history; source/field authority is unchanged. */
    void clear_ring_memo() noexcept;
    /** Read instance-owned interval occupancy without lookup/admission or source work.
     * Workflow: the serialized Host caller owns preparation -> copy size only.
     * This numerical-history observation grants no source/field qualification.
     */
    std::size_t ring_memo_size() const noexcept { return ring_memo_.size(); }
    void require_current_ring(const arch::elliptic::CompositePoisson&,
        const RingBoundaryEvaluation&) const;
    // Lift a current producer only after exact root source/observer equality
    // is proved; unsupported/rounded geometry retains Unknown scope.
    std::vector<arch::elliptic::NativeRzFacePotentialError> root_scoped_ring_errors(
        const arch::elliptic::CompositePoisson&,const RingBoundaryEvaluation&) const;
    RingRhsAssessment assess_ring_rhs(const arch::elliptic::CompositePoisson&,
        const RingBoundaryEvaluation&,std::span<const double> computed_source,
        std::span<const double> computed_rhs,std::span<const double> potential,
        std::span<const double> computed_residual,double rtol,double atol) const;
    // Original request for the ideal native discrete problem, not continuous
    // Phi/force/full RZ science. Real producer scope is checked internally.
    RingRhsAssessment assess_native_ring_rhs(const arch::elliptic::CompositePoisson&,
        const RingBoundaryEvaluation&,std::span<const double> computed_source,
        std::span<const double> computed_rhs,std::span<const double> potential,
        std::span<const double> computed_residual,double rtol,double atol) const;
    std::vector<double> values(const arch::elliptic::CompositePoisson&,double G,
                               double theta=0.25,int order=2) const;
    const auto& nodes() const {return nodes_;}
    const auto& layers() const {return layers_;}
    const auto& volumes() const {return volumes_;}
    const auto& moments() const {return moments_;}
private:
    friend class SelfGravity; // Private stage borrower reads actual generation only.
    /** Exact raw or axial-quotient geometry/G/eligibility key with a mode tag.
     * Both TwoSum terms survive; scalar potential reflection is not force parity.
     */
    struct RingMemoKey {
        std::array<std::uint64_t,11> words{};
        bool operator==(const RingMemoKey&) const = default;
    };
    /** Hash exact bit words; equality, rather than hash collision, authorizes reuse. */
    struct RingMemoHash {
        std::size_t operator()(const RingMemoKey&) const noexcept;
    };
    /** Original Bounded signed interval, its strictly positive density and work cost. */
    struct RingMemoEntry {
        finite_ring_detail::SignedInterval interval;
        double density=0.;
        std::uint64_t leaf_boxes=0;
    };
    /** Try the current request against mathematical history, then the unchanged kernel. */
    RingPotentialEnclosure memoized_ring_potential(double rl,double rh,double zl,double zh,
        double density,double ro,double zo,double G,const RingEnclosureControl&,
        bool quartet,RingBoundaryScalars&) const;
    static constexpr std::size_t maximum_ring_memo_entries=65536;
    // Serialized CPU controller owns this cache. It stores no source, lease or face result.
    mutable std::unordered_map<RingMemoKey,RingMemoEntry,RingMemoHash> ring_memo_;
    void require_ring_operator(const arch::elliptic::CompositePoisson&) const;
    arch::elliptic::CartesianMesh bound_mesh_;
    arch::elliptic::BoundaryKind bound_boundary_;
    std::vector<arch::elliptic::CompositeCell> bound_cells_;
    amr::TopologyEpoch bound_topology_{};
    std::optional<GravitySolveIdentity> source_identity_;
    std::vector<double> ring_density_;
    std::uint64_t source_generation_=0;
    int dimension_=3;
    bool finite_ring_=false;
    double reference_radius_=1.;
    std::vector<BoundaryTreeNode> nodes_;
    std::vector<std::vector<int>> layers_;
    std::vector<double> volumes_;
    std::vector<BoundaryMoments> moments_;
};
} // namespace Physical::Gravity
