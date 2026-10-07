/**
 * @file NativeRzRegridTransfer.h
 * @brief Shared physical-primitive prolongation of one native RZ cell family.
 *
 * Workflow:
 * 1. Borrow one immutable native frame and its real logical/child geometry.
 * 2. Build the existing density/inertia closure on five actual source cells.
 * 3. Integrate the center baseline B and a bounded primitive profile H with
 *    the existing Gauss4x2 V/W rule and density-weighted species leaf.
 * 4. Remove each H-B family mean under its own measure, then use one common
 *    positivity contraction for fluid/rhoX; signed ENUC stays a scalar mean.
 * 5. Publish only provisional native means. The completed destination-domain
 *    BC/exchange owner must subsequently check its actual density, I_* and EOS.
 *
 * This numerical reconstruction neither certifies an unknown subcell inertia
 * nor applies Cartesian kinetic admissibility to the stored m_phi=J/W.
 * No EOS, floor, repair, topology inference or persistent cache is introduced.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "amr/transfer/RegridTransferMath.h"
#include "core/ArchPortability.h"
#include "grid/GridMetrics.h"
#include "numerics/state/RzCellAverage.h"
#include "numerics/state/RzNativeClosure.h"

namespace amr::regrid_math {

/** Actual positive native destination bounds; no inferred storage halo. */
struct NativeRzChildBounds {
    double radial_lower=0.,radial_upper=0.,axial_lower=0.,axial_upper=0.;
};

/** One borrowed source frame and its four actual 2x2 children.
 * Child order is bit0=upper radial half, bit1=upper axial half.
 * logical_nx/ny are real cell extents, not padded strides. Readers supplied
 * below must resolve every center/neighbor density support in the same frame.
 */
struct NativeRzProlongationContext {
    GridMetrics::GeometryView source_geometry;
    int logical_nx=0,logical_ny=0,radial_i=0,axial_j=0;
    std::array<NativeRzChildBounds,4> children{};
    // Actual destination generator/layout and the lower 2x2 member. Standalone
    // unbound math contexts retain strict physical partition certification.
    bool has_destination_geometry=false;
    GridMetrics::GeometryView destination_geometry{};
    int destination_nx=0,destination_ny=0,fine_i=0,fine_j=0;
};

namespace native_rz_detail {

/** Primitive slopes in physical r,z; density has only an axial residual. */
struct Slopes {
    double radial=0.,axial=0.;
};

/** Existing minmod policy on actual physical neighbor distances.
 * Overflow/nonfinite differences are failures, not a silently zero slope.
 */
ARCH_INLINE bool slopes(double center,double left,double right,double lower,
    double upper,double dr,double dz,Slopes& result)
{
    const double a=(center-left)/dr,b=(right-center)/dr;
    const double c=(center-lower)/dz,d=(upper-center)/dz;
    if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(c)||!std::isfinite(d))return false;
    result={minmod(a,b),minmod(c,d)};return true;
}

/** Borrow the per-family primitive-gradient cache in existing unused slots.
 * Slots 0..3 remain child deviations, 4/5 store r/z gradients, and slot 6 is
 * temporary Xi. Lifetime is only this scalar call; no persistent profile cache.
 */
ARCH_INLINE Slopes cached_fraction_slopes(const double* deviation,int species)
{
    const auto offset=static_cast<std::size_t>(species)*maximum_children;
    return {deviation[offset+4],deviation[offset+5]};
}

/** Required nonnegative species representation, never an abundance floor.
 * Strictly positive rho and Xi must not yield zero or a nonfinite rhoX.
 * An exact Xi=0 remains a legitimate zero; signed residual D is not tested here.
 */
ARCH_INLINE bool species_product(double density,double fraction,double& product)
{
    if(!std::isfinite(density)||!(density>0.)||!std::isfinite(fraction)||fraction<0.)return false;
    product=density*fraction;
    return std::isfinite(product)&&product>=0.&&(fraction==0.||product>0.);
}

/** Decode positive rhoX without quietly erasing a positive represented mass. */
ARCH_INLINE bool species_fraction(double mass,double density,double& fraction)
{
    if(!std::isfinite(mass)||mass<0.||!std::isfinite(density)||!(density>0.))return false;
    fraction=mass/density;
    return std::isfinite(fraction)&&fraction>=0.&&(mass==0.||fraction>0.);
}

/** Check five actual family integrals against the actual native source.
 * Weights are M_child/M_parent, not child-sum-normalized means.
 * Scale_j=max(|parent_j|,max_k|child_k,j|), and compare
 * sum_k w_k*(child_k,j/scale_j) with parent_j/scale_j using the existing
 * 64*epsilon policy. V applies except W for m_phi. Normalized comparisons
 * retain representable subnormal source momenta without a density allowance;
 * scale=0 is valid only when the parent and every child component are zero.
 * This is a fail-closed conservation check, never a correction to B's E/J.
 */
ARCH_INLINE bool family_matches_source(const FluidVector& source,const FluidVector* family,
    const std::array<double,4>& volume_weights,const std::array<double,4>& angular_weights)
{
    FluidVector parent=source;
    for(int field=0;field<5;++field) {
        const double target=component(parent,field);
        if(!std::isfinite(target))return false;
        double scale=std::abs(target);
        for(int child=0;child<4;++child) {
            FluidVector sample=family[child];const double value=component(sample,field);
            if(!std::isfinite(value))return false;
            scale=maximum(scale,std::abs(value));
        }
        if(scale==0.)continue; // The finite maximum proves every value is zero.
        double average=0.;
        for(int child=0;child<4;++child) {
            FluidVector sample=family[child];
            average+=(field==3?angular_weights[child]:volume_weights[child])
                *(component(sample,field)/scale);
        }
        if(!std::isfinite(average)||std::abs(average-target/scale)
            >64.*std::numeric_limits<double>::epsilon())return false;
    }
    return true;
}

/** Physical affine value; theta contracts every primitive residual together. */
ARCH_INLINE double value(double center,const Slopes& slope,double dr,double dz,double theta)
{
    return theta==0. ? center : center+theta*(slope.radial*dr+slope.axial*dz);
}

/** Intersect a linear lower bound along B+theta*d without a new floor.
 * Baseline must itself satisfy the true bound. Only a strict zero boundary
 * requests the existing 64-epsilon inward contraction after all intersections.
 */
ARCH_INLINE bool lower_bound(double base,double residual,double bound,bool strict,
    double& theta,bool& inward)
{
    if(!std::isfinite(base)||!std::isfinite(residual)||!std::isfinite(bound)
        ||base<bound||(strict&&!(base>bound)))return false;
    const double candidate=base+residual;
    if(!std::isfinite(candidate))return false;
    if(candidate<bound||(strict&&!(candidate>bound))) {
        if(!(residual<0.))return false;
        const double limit=(base-bound)/(-residual);
        if(!std::isfinite(limit)||limit<0.)return false;
        if(limit<theta) {theta=limit;inward=strict;}
        else if(limit==theta&&strict)inward=true;
    }
    return true;
}

/** Upper-energy affine inequality; no convexity assertion about native U. */
ARCH_INLINE bool upper_bound(double base,double residual,double bound,double& theta)
{
    if(!std::isfinite(base)||!std::isfinite(residual)||base>bound)return false;
    const double candidate=base+residual;
    if(!std::isfinite(candidate))return false;
    if(candidate>bound) {
        if(!(residual>0.))return false;
        const double limit=(bound-base)/residual;
        if(!std::isfinite(limit)||limit<0.)return false;
        theta=minimum(theta,limit);
    }
    return true;
}

/** Separate projection means, physical integral ratios and source chart image.
 * n_k=M_k/sum M controls zero-mean residuals; a_k=M_k/M_parent authenticates
 * actual conserved integrals. They cannot substitute for geometry identity.
 */
struct FamilyGeometry {
    std::array<double,4> volume_mean{},angular_mean{},volume_integral{},angular_integral{};
    double axial_image_shift=0.;
};

/** Authenticate actual source/destination generators and signed 2:1 cells.
 * Cross-level rounded physical endpoints are not equality oracles. Root bounds,
 * counts and periodic authorization must match bitwise; fine global index is
 * twice the real coarse index. Only a paired periodic z rule admits +/- one
 * genuine root-domain image. Radial images and blind modulo are forbidden.
 */
ARCH_INLINE bool dyadic_mapping(const NativeRzProlongationContext& context,double& z_shift)
{
    const auto& source=context.source_geometry;
    const auto& fine=context.destination_geometry;
    if(!context.has_destination_geometry||!GridMetrics::matches_identity(source)
       ||!GridMetrics::matches_identity(fine))return false;
    const auto& a=source.dyadic_identity;const auto& b=fine.dyadic_identity;
    if(b.level!=a.level+1)return false;
    auto root_b=b;root_b.level=a.level;root_b.logical=a.logical;
    if(!GridMetrics::equal_identity(a,root_b))return false;
    for(int axis=0;axis<2;++axis) {
        std::int64_t coarse=0,child=0;
        const int source_index=axis==0?context.radial_i:context.axial_j;
        const int fine_index=axis==0?context.fine_i:context.fine_j;
        if(!GridMetrics::global_cell(a,axis,std::int64_t(source_index)-source.ng,coarse)
           ||!GridMetrics::global_cell(b,axis,std::int64_t(fine_index)-fine.ng,child)
           ||coarse<std::numeric_limits<std::int64_t>::min()/2
           ||coarse>std::numeric_limits<std::int64_t>::max()/2)return false;
        const auto expected=2*coarse;
        if(child==expected)continue;
        if(axis!=1||!a.periodic_axial)return false;
        // Existing Morton/layout bounds make all actual indices small signed
        // values, but check before general integer subtraction nevertheless.
        if((expected<0&&child>std::numeric_limits<std::int64_t>::max()+expected)
           ||(expected>0&&child<std::numeric_limits<std::int64_t>::min()+expected))return false;
        const auto difference=child-expected;
        const auto extent=std::int64_t(b.root_blocks[1])*amr::BLOCK_NY
            *(std::int64_t{1}<<b.level);
        if(difference!=extent&&difference!=-extent)return false;
        z_shift=(difference>0?1.:-1.)*(a.root_upper[1]-a.root_lower[1]);
        if(!std::isfinite(z_shift))return false;
    }
    return true;
}

/** Authenticate geometry, then form actual V/W integral and mean weights.
 * Generated grids prove their own coordinates plus exact logical partition;
 * unbound standalone math must prove every physical outer/midpoint equality.
 * Fine pairwise contiguity remains exact in both paths. Shared canonical
 * axial faces own the actual source/child height used by GridMetrics V/W.
 * Unbound partition checks retain their existing strict physical midpoint rule.
 * No tolerance, rescaling or invented cell is introduced.
 */
ARCH_INLINE bool geometry(const NativeRzProlongationContext& context,FamilyGeometry& weights)
{
    const auto& g=context.source_geometry;
    const int nx=context.logical_nx,ny=context.logical_ny,i=context.radial_i,j=context.axial_j;
    if(g.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
       ||g.geometry!=GridMetrics::Geometry::Cylindrical||g.dim!=2||g.ng<1
       ||nx<3||ny<3||g.stride_y<nx||g.stride_z<=0
       ||ny>g.stride_z/g.stride_y||g.total_size<g.stride_z
       ||i<1||i>=nx-1||j<1||j>=ny-1
       ||!std::isfinite(g.dx1)||!(g.dx1>0.)
       ||!std::isfinite(g.dx2)||!(g.dx2>0.))return false;
    const double rl=g.GetFacePosL(i),rr=g.GetFacePosR(i);
    const double zl=g.GetAxialFacePosL(j),zr=g.GetAxialFacePosR(j);
    if(!std::isfinite(rl)||rl<0.||!std::isfinite(rr)||!(rr>rl)
       ||!std::isfinite(zl)||!std::isfinite(zr)||!(zr>zl))return false;
    const auto& c=context.children;
    for(int k=0;k<4;++k) {
        const auto& q=c[k];
        if(!std::isfinite(q.radial_lower)||q.radial_lower<0.
            ||!std::isfinite(q.radial_upper)||!(q.radial_upper>q.radial_lower)
            ||!std::isfinite(q.axial_lower)||!std::isfinite(q.axial_upper)
            ||!(q.axial_upper>q.axial_lower))return false;
    }
    if(c[0].radial_upper!=c[1].radial_lower||c[2].radial_upper!=c[3].radial_lower
       ||c[0].radial_upper!=c[2].radial_upper
       ||c[0].axial_upper!=c[2].axial_lower||c[1].axial_upper!=c[3].axial_lower
       ||c[0].axial_upper!=c[1].axial_upper)return false;
    if(context.has_destination_geometry) {
        const auto& f=context.destination_geometry;
        const int fi=context.fine_i,fj=context.fine_j;
        if(f.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
           ||f.geometry!=GridMetrics::Geometry::Cylindrical||f.dim!=2
           ||context.destination_nx<2||context.destination_ny<2
           ||f.stride_y<context.destination_nx||f.stride_z<=0
           ||context.destination_ny>f.stride_z/f.stride_y||f.total_size<f.stride_z
           ||fi<0||fi>=context.destination_nx-1||fj<0||fj>=context.destination_ny-1
           ||!std::isfinite(f.dx1)||!(f.dx1>0.)
           ||!std::isfinite(f.dx2)||!(f.dx2>0.))return false;
        for(int k=0;k<4;++k) {
            const int ci=fi+(k&1),cj=fj+((k>>1)&1);
            const auto& q=c[k];
            if(q.radial_lower!=f.GetFacePosL(ci)||q.radial_upper!=f.GetFacePosR(ci)
               ||q.axial_lower!=f.GetAxialFacePosL(cj)
               ||q.axial_upper!=f.GetAxialFacePosR(cj))return false;
        }
    }
    const bool bound=g.dyadic_identity.bound;
    if(bound) {
        if(!dyadic_mapping(context,weights.axial_image_shift))return false;
    } else {
        if(context.has_destination_geometry&&context.destination_geometry.dyadic_identity.bound)return false;
        // No unbound fragment or claimed flag acquires logical authority.
        if(c[0].radial_lower!=rl||c[2].radial_lower!=rl
           ||c[1].radial_upper!=rr||c[3].radial_upper!=rr
           ||c[0].axial_lower!=zl||c[1].axial_lower!=zl
           ||c[2].axial_upper!=zr||c[3].axial_upper!=zr
           ||c[0].radial_upper!=rl+.5*(rr-rl)||c[0].axial_upper!=zl+.5*(zr-zl))return false;
    }
    double v=0.,w=0.;
    const double pv=GridMetrics::CellVolume(g,i,j,0);
    const double pw=GridMetrics::Rz::AngularMomentumMeasure(g,i,j);
    if(!std::isfinite(pv)||!(pv>0.)||!std::isfinite(pw)||!(pw>0.))return false;
    for(int k=0;k<4;++k) {
        const auto& q=c[k];
        // Bound destinations were authenticated against their actual axial
        // faces above; standalone children retain their supplied strict split.
        const double dz=context.has_destination_geometry
            ?context.destination_geometry.CellWidth(1,context.fine_j+((k>>1)&1))
            :q.axial_upper-q.axial_lower;
        const double cv=GridMetrics::Rz::CellVolume(q.radial_lower,q.radial_upper,dz);
        const double cw=GridMetrics::Rz::AngularMomentumMeasure(q.radial_lower,q.radial_upper,dz);
        if(!std::isfinite(cv)||!(cv>0.)||!std::isfinite(cw)||!(cw>0.))return false;
        weights.volume_mean[k]=cv;weights.angular_mean[k]=cw;
        weights.volume_integral[k]=cv/pv;weights.angular_integral[k]=cw/pw;
        if(!std::isfinite(weights.volume_integral[k])||!(weights.volume_integral[k]>0.)
           ||!std::isfinite(weights.angular_integral[k])||!(weights.angular_integral[k]>0.))return false;
        v+=cv;w+=cw;
    }
    if(!std::isfinite(v)||!(v>0.)||!std::isfinite(w)||!(w>0.))return false;
    for(int k=0;k<4;++k) {weights.volume_mean[k]/=v;weights.angular_mean[k]/=w;}
    return true;
}

/** Construct physical point U with unchanged kinetic formula.
 * u_phi=Omega*r; E=rho*e+(m_r*u_r+m_z*u_z+m_phi*u_phi)/2.
 * Nonrepresentable arithmetic is rejected by the ordinary shared point gate.
 */
ARCH_INLINE FluidVector physical_point(double rho,double ur,double uz,double omega,
    double internal,double radius)
{
    const double uphi=omega*radius,mr=rho*ur,mz=rho*uz,mp=rho*uphi;
    return {rho,mr,mz,mp,rho*internal+(.5*mr)*ur+(.5*mz)*uz+(.5*mp)*uphi};
}

/** Strict ordinary point gate only after native U has become a physical point. */
ARCH_INLINE bool point_valid(const FluidVector& state,const arch::state::Bounds& bounds)
{
    return arch::state::validate(state,nullptr,0,1,bounds.density,
        bounds.internal_min,bounds.internal_max)==arch::state::Status::valid;
}

} // namespace native_rz_detail

/** Prolong one native parent with immutable source readers and 17*S scratch.
 * Readers: state(index)->FluidVector, enuc(index)->double, fraction(s,index)->Xi.
 * B uses the actual density polynomial and constant physical u_r/u_z/Omega/e;
 * H adds physical minmod primitive slopes. For each component,
 * D_k=H_k-B_k-sum_l(w_l*(H_l-B_l)); C_k=B_k+theta*D_k.
 * w is V for rho/m_r/m_z/E/rhoX and W for m_phi. One theta preserves all
 * species/fluid integrals together. No fixed-I thermal bisection is valid here.
 * Result publication is atomic at this scalar boundary; workspace is borrowed
 * scratch and may change on failure. Completed-domain destination EOS remains
 * mandatory, even on theta=0. This function never claims that gate passed.
 */
template<class StateReader,class EnucReader,class FractionReader>
ARCH_INLINE Status prolong_native_family(const NativeRzProlongationContext& context,
    const StateReader& read,const EnucReader& enuc,const FractionReader& fraction,
    int species,const arch::state::Bounds& bounds,double* workspace,ProlongationResult& result)
{
    using namespace native_rz_detail;
    FamilyGeometry measures{};
    if(species<0||(species>0&&!workspace)||!arch::state::valid_bounds(bounds)
       ||!geometry(context,measures))return Status::InvalidGeometry;
    const auto& vw=measures.volume_mean;const auto& ww=measures.angular_mean;
    const auto& g=context.source_geometry;const int i=context.radial_i,j=context.axial_j;
    const int indices[]{g.GetIndex(i,j),g.GetIndex(i-1,j),g.GetIndex(i+1,j),
        g.GetIndex(i,j-1),g.GetIndex(i,j+1)};
    const int columns[]{i,i-1,i+1,i,i};
    std::array<RzThermodynamics::Cell,5> cells{};
    double* q=workspace;
    double* dq=species>0?workspace+static_cast<std::size_t>(species)*maximum_children:nullptr;
    double* parent_X=species>0?workspace+static_cast<std::size_t>(species)*(2*maximum_children):nullptr;
    for(int n=0;n<5;++n) {
        for(int s=0;s<species;++s)dq[s]=fraction(s,indices[n]);
        const auto state=read(indices[n]);
        if(RzThermodynamics::provisional_native_state(state,nullptr,0,1,bounds)
            !=arch::state::Status::valid)return Status::ParentFluid;
        if(arch::state::validate_composition(dq,species,1)!=arch::state::Status::valid)
            return Status::ParentFractions;
        const int support=columns[n]-1<0?0:columns[n]-1>context.logical_nx-3
            ?context.logical_nx-3:columns[n]-1;
        cells[n]=RzThermodynamics::make_cell_supported(read,indices[n],g,columns[n],support,bounds);
        if(!cells[n].valid())return Status::ParentFluid;
        for(int node=0;node<RzThermodynamics::physical_node_count;++node)
            if(!point_valid(RzThermodynamics::base_point(cells[n],
                RzThermodynamics::physical_node_radius(cells[n],node)),bounds))return Status::ParentFluid;
        if(!std::isfinite(enuc(indices[n])))return Status::ProlongationEnuc;
    }
    const auto& base=cells[0];const auto parent=read(indices[0]);
    const double rc=g.GetCellCenterX(i),zc=g.GetCellCenterY(j)+measures.axial_image_shift;
    if(!std::isfinite(rc)||!std::isfinite(zc))return Status::InvalidGeometry;
    Slopes rho_slope{},ur{},uz{},omega{},internal{};
    // Radial rho is solely the shared positive quadratic, never a new slope.
    const double rho_low=(parent.rho-read(indices[3]).rho)/g.dx2;
    const double rho_high=(read(indices[4]).rho-parent.rho)/g.dx2;
    if(!std::isfinite(rho_low)||!std::isfinite(rho_high))return Status::ParentFluid;
    rho_slope.axial=minmod(rho_low,rho_high);
    if(!slopes(base.radial_velocity,cells[1].radial_velocity,cells[2].radial_velocity,
            cells[3].radial_velocity,cells[4].radial_velocity,g.dx1,g.dx2,ur)
       ||!slopes(base.axial_velocity,cells[1].axial_velocity,cells[2].axial_velocity,
            cells[3].axial_velocity,cells[4].axial_velocity,g.dx1,g.dx2,uz)
       ||!slopes(base.omega,cells[1].omega,cells[2].omega,cells[3].omega,cells[4].omega,g.dx1,g.dx2,omega)
       ||!slopes(base.internal,cells[1].internal,cells[2].internal,cells[3].internal,
            cells[4].internal,g.dx1,g.dx2,internal))return Status::ParentFluid;
    int closure=-1;double parent_sum=0.;
    for(int s=0;s<species;++s) {
        parent_X[s]=fraction(s,indices[0]);parent_sum+=parent_X[s];
        if(closure<0||parent_X[s]>parent_X[closure])closure=s;
    }
    const double tolerance=composition_simplex_tolerance(species);
    if(species>0) {
        if(!std::isfinite(parent_sum)||std::abs(parent_sum-1.)>tolerance)return Status::ParentNormalization;
        parent_X[closure]+=1.-parent_sum;
        if(!std::isfinite(parent_X[closure])||parent_X[closure]<0.)return Status::ParentClosure;
    }
    // N-1 independent minmods are read once, in ascending species order.
    // Largest Xi is dependent: its gradient is minus their ordered sum, so
    // sum Xi(r,z)=1 mathematically. Every gradient uses the same theta later.
    Slopes closure_gradient{};
    for(int s=0;s<species;++s) {
        double parent_mass=0.;
        if(!species_product(parent.rho,parent_X[s],parent_mass))return Status::SpeciesIntegral;
        if(s==closure)continue;
        Slopes gradient{};
        if(!slopes(fraction(s,indices[0]),fraction(s,indices[1]),fraction(s,indices[2]),
            fraction(s,indices[3]),fraction(s,indices[4]),g.dx1,g.dx2,gradient))return Status::ParentFractions;
        const auto offset=static_cast<std::size_t>(s)*maximum_children;
        dq[offset+4]=gradient.radial;dq[offset+5]=gradient.axial;
        closure_gradient.radial-=gradient.radial;closure_gradient.axial-=gradient.axial;
        if(!std::isfinite(closure_gradient.radial)||!std::isfinite(closure_gradient.axial))return Status::ParentFractions;
    }
    if(species>0) {
        const auto offset=static_cast<std::size_t>(closure)*maximum_children;
        dq[offset+4]=closure_gradient.radial;dq[offset+5]=closure_gradient.axial;
    }
    double theta_profile=1.;bool inward=false;
    const double radii[]{base.density.lower,base.density.upper};
    double rho_min=0.,rho_max=0.;
    // Reuse the density authority's endpoint/interior-vertex extrema, rather
    // than rebuilding a second quadratic minimum or sampling it heuristically.
    if(!RzDensity::detail::extrema(base.density.density,
        (radii[0]-base.density.origin)/base.density.spacing,
        (radii[1]-base.density.origin)/base.density.spacing,rho_min,rho_max))return Status::ParentFluid;
    const double zl=context.children[0].axial_lower,zr=context.children[2].axial_upper;
    for(int z=0;z<2;++z)
        if(!lower_bound(rho_min,rho_slope.axial*((z?zr:zl)-zc),bounds.density,
            bounds.density==0.,theta_profile,inward))return Status::ParentFluid;
    for(int r=0;r<2;++r)for(int z=0;z<2;++z) {
        const double dr=radii[r]-rc,dz=(z?zr:zl)-zc;
        const double delta=internal.radial*dr+internal.axial*dz;
        if(!lower_bound(base.internal,delta,bounds.internal_min,bounds.internal_min==0.,theta_profile,inward)
           ||!upper_bound(base.internal,delta,bounds.internal_max,theta_profile))return Status::ParentFluid;
        for(int s=0;s<species;++s) {
            const auto x=cached_fraction_slopes(dq,s);
            if(!lower_bound(parent_X[s],x.radial*dr+x.axial*dz,0.,false,theta_profile,inward))
                return Status::ParentFractions;
        }
    }
    theta_profile=unit_clamp(theta_profile);
    if(inward&&theta_profile<1.)theta_profile*=1.-64.*std::numeric_limits<double>::epsilon();
    std::array<FluidVector,4> baseline{},deviation{};
    ProlongationResult pending{};
    for(int child=0;child<4;++child) {
        const auto& b=context.children[child];
        const auto samples=GridMetrics::Rz::CellAverageSamples(b.radial_lower,b.radial_upper,b.axial_lower,b.axial_upper);
        std::array<FluidVector,8> low{},high{};
        std::array<double,8> weights{},densities{};
        for(int node=0;node<8;++node) {
            const auto& sample=samples[node];const double dr=sample.radius-rc,dz=sample.axial-zc;
            low[node]=RzThermodynamics::base_point(base,sample.radius);
            const double rho=low[node].rho+theta_profile*rho_slope.axial*dz;
            high[node]=physical_point(rho,value(base.radial_velocity,ur,dr,dz,theta_profile),
                value(base.axial_velocity,uz,dr,dz,theta_profile),value(base.omega,omega,dr,dz,theta_profile),
                value(base.internal,internal,dr,dz,theta_profile),sample.radius);
            if(!point_valid(low[node],bounds)||!point_valid(high[node],bounds))return Status::FineFluid;
            weights[node]=sample.volume_weight;densities[node]=high[node].rho;
            for(int s=0;s<species;++s) {
                const auto x=cached_fraction_slopes(dq,s);
                dq[static_cast<std::size_t>(s)*maximum_children+6]
                    =value(parent_X[s],x,dr,dz,theta_profile);
            }
            if(arch::state::validate_composition(species>0?dq+6:nullptr,species,maximum_children)
                !=arch::state::Status::valid)return Status::CompositionProjection;
        }
        auto bmean=RzCellAverage::conserved_mean(samples,[&](std::size_t n){return low[n];});
        auto hmean=RzCellAverage::conserved_mean(samples,[&](std::size_t n){return high[n];});
        if(!bmean.valid()||!hmean.valid())return Status::FineFluid;
        baseline[child]=bmean.value;
        for(int field=0;field<5;++field) {
            component(deviation[child],field)=component(hmean.value,field)-component(bmean.value,field);
            if(!std::isfinite(component(deviation[child],field)))return Status::FineFluid;
        }
        for(int s=0;s<species;++s) {
            const auto x=cached_fraction_slopes(dq,s);
            const auto xm=RzCellAverage::fraction_mean(weights,densities,[&](std::size_t n) {
                return value(parent_X[s],x,samples[n].radius-rc,samples[n].axial-zc,theta_profile);
            });
            if(!xm.valid())return Status::CompositionProjection;
            double baseline_mass=0.;
            if(!species_product(baseline[child].rho,parent_X[s],baseline_mass)
               ||!species_product(hmean.value.rho,xm.value,
                    q[static_cast<std::size_t>(s)*maximum_children+child]))return Status::SpeciesIntegral;
            dq[static_cast<std::size_t>(s)*maximum_children+child]
                =q[static_cast<std::size_t>(s)*maximum_children+child]-baseline_mass;
            if(!std::isfinite(q[static_cast<std::size_t>(s)*maximum_children+child])
                ||!std::isfinite(dq[static_cast<std::size_t>(s)*maximum_children+child]))return Status::SpeciesIntegral;
        }
        const double ea=enuc(indices[0])-enuc(indices[1]),eb=enuc(indices[2])-enuc(indices[0]);
        const double ec=enuc(indices[0])-enuc(indices[3]),ed=enuc(indices[4])-enuc(indices[0]);
        if(!std::isfinite(ea)||!std::isfinite(eb)||!std::isfinite(ec)||!std::isfinite(ed))
            return Status::ProlongationEnuc;
        const double sx=minmod(ea,eb),sy=minmod(ec,ed);
        pending.enuc[child]=enuc(indices[0])+((child&1)?.25:-.25)*sx+((child&2)?.25:-.25)*sy;
        if(!std::isfinite(sx)||!std::isfinite(sy)||!std::isfinite(pending.enuc[child]))return Status::ProlongationEnuc;
    }
    if(!family_matches_source(parent,baseline.data(),measures.volume_integral,measures.angular_integral))return Status::ParentFluid;
    for(int field=0;field<5;++field) {
        double mean=0.;for(int child=0;child<4;++child)
            mean+=component(deviation[child],field)*(field==3?ww[child]:vw[child]);
        if(!std::isfinite(mean))return Status::FineFluid;
        for(int child=0;child<4;++child) {
            component(deviation[child],field)-=mean;
            if(!std::isfinite(component(deviation[child],field)))return Status::FineFluid;
        }
    }
    for(int s=0;s<species;++s) {
        double mean=0.;for(int child=0;child<4;++child)mean+=dq[static_cast<std::size_t>(s)*maximum_children+child]*vw[child];
        if(!std::isfinite(mean))return Status::SpeciesIntegral;
        for(int child=0;child<4;++child) {
            auto& residual=dq[static_cast<std::size_t>(s)*maximum_children+child];
            residual-=mean;if(!std::isfinite(residual))return Status::SpeciesIntegral;
        }
    }
    // Close each residual species row only within the existing roundoff rule.
    if(species>0)for(int child=0;child<4;++child) {
        double sum=0.;for(int s=0;s<species;++s)sum+=dq[static_cast<std::size_t>(s)*maximum_children+child];
        const double mismatch=deviation[child].rho-sum;
        const double scale=maximum(baseline[child].rho,std::abs(deviation[child].rho));
        if(!std::isfinite(mismatch)||std::abs(mismatch)>tolerance*scale)return Status::CompositionClosure;
        dq[static_cast<std::size_t>(closure)*maximum_children+child]+=mismatch;
    }
    double theta=1.;bool final_inward=false;
    for(int child=0;child<4;++child) {
        if(!lower_bound(baseline[child].rho,deviation[child].rho,bounds.density,
            bounds.density==0.,theta,final_inward))return Status::FineFluid;
        for(int s=0;s<species;++s) {
            double baseline_mass=0.;
            if(!species_product(baseline[child].rho,parent_X[s],baseline_mass))return Status::SpeciesIntegral;
            if(!lower_bound(baseline_mass,dq[static_cast<std::size_t>(s)*maximum_children+child],
                0.,false,theta,final_inward))return Status::CompositionProjection;
        }
    }
    theta=unit_clamp(theta);
    if(final_inward&&theta<1.)theta*=1.-64.*std::numeric_limits<double>::epsilon();
    // ENUC retains the existing independent signed scalar correction. It has
    // no thermodynamic role and does not use the joint fluid/species theta.
    double enuc_mean=0.;for(int child=0;child<4;++child)enuc_mean+=pending.enuc[child]*vw[child];
    const double enuc_shift=enuc(indices[0])-enuc_mean;
    for(int child=0;child<4;++child) {
        for(int field=0;field<5;++field)component(pending.fluid[child],field)=theta==0.
            ?component(baseline[child],field):component(baseline[child],field)+theta*component(deviation[child],field);
        if(RzThermodynamics::provisional_native_state(pending.fluid[child],nullptr,0,1,bounds)
            !=arch::state::Status::valid)return Status::FineFluid;
        double sum=0.;
        for(int s=0;s<species;++s) {
            const auto offset=static_cast<std::size_t>(s)*maximum_children+child;
            double baseline_mass=0.;
            if(!species_product(baseline[child].rho,parent_X[s],baseline_mass))return Status::SpeciesIntegral;
            q[offset]=theta==0.?baseline_mass:baseline_mass+theta*dq[offset];
            if(!std::isfinite(q[offset])||q[offset]<0.)return Status::CompositionProjection;
            sum+=q[offset];
        }
        if(species>0) {
            const double error=pending.fluid[child].rho-sum;
            if(!std::isfinite(error)||std::abs(error)>tolerance*pending.fluid[child].rho)return Status::CompositionClosure;
            q[static_cast<std::size_t>(closure)*maximum_children+child]+=error;
            if(!std::isfinite(q[static_cast<std::size_t>(closure)*maximum_children+child])
               ||q[static_cast<std::size_t>(closure)*maximum_children+child]<0.)return Status::CompositionClosure;
        }
        pending.enuc[child]+=enuc_shift;
        if(!std::isfinite(pending.enuc[child]))return Status::ProlongationEnuc;
        // Slot 6 is Xi scratch: child deviations0..3 and gradients4/5 survive.
        for(int s=0;s<species;++s) {
            const auto offset=static_cast<std::size_t>(s)*maximum_children;
            double represented_mass=0.;
            if(!species_fraction(q[offset+child],pending.fluid[child].rho,dq[offset+6])
               ||!species_product(pending.fluid[child].rho,dq[offset+6],represented_mass))return Status::SpeciesIntegral;
        }
        if(RzThermodynamics::provisional_native_state(pending.fluid[child],
            species>0?dq+6:nullptr,species,maximum_children,bounds)
            !=arch::state::Status::valid)return Status::FineFluid;
    }
    // Independently verify columns after the sole roundoff row correction.
    for(int s=0;s<species;++s) {
        double mass=0.;for(int child=0;child<4;++child)mass+=q[static_cast<std::size_t>(s)*maximum_children+child]*measures.volume_integral[child];
        double target=0.;
        if(!species_product(parent.rho,parent_X[s],target))return Status::SpeciesIntegral;
        if(!std::isfinite(mass)||!std::isfinite(target)
           ||std::abs(mass-target)>tolerance*parent.rho)return Status::SpeciesIntegral;
    }
    if(!family_matches_source(parent,pending.fluid,measures.volume_integral,measures.angular_integral))return Status::FineFluid;
    pending.rhoX=workspace;result=pending;return Status::Ok;
}

} // namespace amr::regrid_math
