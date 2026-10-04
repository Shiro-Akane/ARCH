/** @file GravityBoundary.h
 * Isolated Newtonian boundary values from the actual leaf mass distribution.
 * Geometry is cached; moments and their evaluation are shared Host/Device math.
 * Workflow:
 * 1. Receive active density with mesh and generation identity.
 * 2. Define the device-shareable multipole leaves and boundary evaluation work.
 * 3. Publish a checked potential/acceleration field for the requested stage.
 */

#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

#include "core/CompensatedSum.h"
#include "physics/gravity/FiniteRingBoundaryMath.h"
#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/GravitySourceBounds.h"
#include "grid/GridGeometryView.h"
#include "grid/GridMetrics.h"
#include "numerics/elliptic/CompositePoisson.h"

namespace Physical::Gravity {
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
enum class RingBoundaryStatus : unsigned char { Bounded, WorkLimit, PrecisionLimit };
struct RingBoundaryEvaluation {
    RingBoundaryStatus status=RingBoundaryStatus::PrecisionLimit;
    GravitySolveIdentity source;
    std::uint64_t source_generation=0,leaf_evaluations=0,range_evaluations=0,
        kernel_enclosures=0,agm_iterations=0,parent_evaluations=0,parent_acceptances=0,
        represented_leaf_evaluations=0;
    std::vector<double> values,lower,upper,far_truncation_upper,far_evaluation_width_upper;
    std::vector<arch::elliptic::BoundaryPotentialError> errors;
};
/** Composed mathematical-source error, conditional on the stored operator.
 * Native geometry/stencil/weights construction remains a mandatory separate
 * physical certificate. No API can upgrade this result into production RZ.
 */
enum class RingRhsAssessmentScope { StoredNativeOperator };
struct RingRhsAssessment {
    GravitySolveIdentity source;
    std::uint64_t source_generation=0;
    RingRhsAssessmentScope scope=RingRhsAssessmentScope::StoredNativeOperator;
    GravitySourceBounds source_error;
    arch::elliptic::BoundaryRhsError boundary_error,combined_rhs_error;
    arch::elliptic::PoissonArithmeticError assembly_error,residual_error;
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
    RingBoundaryEvaluation ring_boundary(const arch::elliptic::CompositePoisson&,
        const GravitySolveIdentity&,const RingBoundaryControl&) const;
    void require_current_ring(const arch::elliptic::CompositePoisson&,
        const RingBoundaryEvaluation&) const;
    RingRhsAssessment assess_ring_rhs(const arch::elliptic::CompositePoisson&,
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
