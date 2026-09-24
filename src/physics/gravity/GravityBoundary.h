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
#include <span>
#include <vector>

#include "core/CompensatedSum.h"
#include "grid/GridGeometryView.h"
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
        const double inverse=1./std::sqrt(r2);
        double term=m.value[0];
        if(order>=1) for(int a=0;a<3;++a) term+=m.value[1+a]*r[a]/r2;
        if(order>=2) {
            double contraction=0.,trace=0.;
            for(int a=0;a<3;++a) {
                trace+=m.value[second_moment_index(a,a)];
                for(int b=0;b<3;++b) contraction+=r[a]*r[b]*m.value[second_moment_index(a,b)];
            }
            term+=(3.*contraction/r2-trace)/(2.*r2);
        }
        // Phi = -G [M/r + D.r/r^3 + (3 r.Q.r/r^2 - tr Q)/(2 r^3)].
        potential.add(-G*inverse*term);
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
class GravityBoundary {
public:
    explicit GravityBoundary(const arch::elliptic::CompositePoisson&);
    void update(std::span<const double> density);
    std::vector<double> values(const arch::elliptic::CompositePoisson&,double G,
                               double theta=0.25,int order=2) const;
    const auto& nodes() const {return nodes_;}
    const auto& layers() const {return layers_;}
    const auto& volumes() const {return volumes_;}
    const auto& moments() const {return moments_;}
private:
    int dimension_=3;
    double reference_radius_=1.;
    std::vector<BoundaryTreeNode> nodes_;
    std::vector<std::vector<int>> layers_;
    std::vector<double> volumes_;
    std::vector<BoundaryMoments> moments_;
};
} // namespace Physical::Gravity
