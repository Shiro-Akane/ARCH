/**
 * @file RzNativeFaceFlux.h
 * @brief Shared native RZ point fluxes and V/W face integration.
 *
 * Workflow:
 * 1. Require the actual selected donor bundles and their physical EOS states.
 * 2. Evaluate the configured Riemann policy at one radial face or four axial
 *    radial Gauss nodes; immutable physical B supplies the LLF baseline.
 * 3. Take ONE minimum point factor over the whole face, including rhoX.
 * 4. Integrate rho/mr/mz/E/rhoX with |r| and mphi with r^2 on axial faces.
 * 5. Blend the two integrated fluxes once and return one publishable face.
 *
 * F=F_low+theta*(F_high-F_low). Axial F_V=<|r|F>/<|r|> and
 * F_W=<r^2 F>/<r^2>; radial faces use the same physical point flux.
 * The divergence/register owner applies its torque measure exactly once.
 * Point LLF screening is not a whole mixed-measure invariant-domain proof;
 * completed native Runtime density/inertia/EOS acceptance remains mandatory.
 * This leaf owns no allocation, backend launch, halo or publication authority.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "core/ArchPortability.h"
#include "numerics/flux/InvariantDomainFlux.h"
#include "numerics/reconstruction/RzSelectedReconstruction.h"

namespace RzNativeFaceFlux {
/** Caller-owned scratch; no fixed species ceiling or persistent second state. */
struct Scratch {
    double* rhoX=nullptr;             // 16*S donor/node conservative fractions.
    double* reconstruction=nullptr;  // 19*S selected-bundle scratch.
    std::size_t reconstruction_count=0;
    double *x_left=nullptr,*x_right=nullptr,*candidate_species=nullptr;
    double *high_sum=nullptr,*low_sum=nullptr; // S each; unpublished until return.
};

/** A flux is signed; finite components, rather than state positivity, apply. */
ARCH_INLINE bool finite_flux(const FluidVector& f) {
    return std::isfinite(f.rho)&&std::isfinite(f.mom_u)&&std::isfinite(f.mom_v)
        &&std::isfinite(f.mom_w)&&std::isfinite(f.eng);
}

/** Integrate distinct native measures without adding another torque lever. */
ARCH_INLINE void add_weighted(FluidVector& sum,const FluidVector& point,
    double volume_weight,double angular_weight) {
    sum.rho+=volume_weight*point.rho;sum.mom_u+=volume_weight*point.mom_u;
    sum.mom_v+=volume_weight*point.mom_v;sum.mom_w+=angular_weight*point.mom_w;
    sum.eng+=volume_weight*point.eng;
}

/** Actual one-sided axial quadrature weights; radius scaling avoids r^2
 * overflow and never replaces a small physical measure with a floor.
 * Gauss weights integrate [-1,1]; the common dr and 2*pi cancel in averages.
 */
ARCH_INLINE bool axial_weights(const GridMetrics::GeometryView& geometry,int i,
    double radius,int node,double& volume_weight,double& angular_weight) {
    const double lower=geometry.GetFacePosL(i),upper=geometry.GetFacePosR(i);
    if(!std::isfinite(lower)||!std::isfinite(upper)||!(upper>lower)
        ||(lower<0.&&upper>0.)||node<0||node>=4||!std::isfinite(radius))return false;
    const double scale=std::max(std::abs(lower),std::abs(upper));
    if(!std::isfinite(scale)||!(scale>0.))return false;
    const double l=lower/scale,u=upper/scale,r=radius/scale;
    const double v=.5*std::abs(l+u),w=(l*l+l*u+u*u)/3.;
    if(!(v>0.)||!(w>0.))return false;
    const double weight=.5*RzReconstruction::quadrature_weight(node);
    volume_weight=weight*std::abs(r)/v;angular_weight=weight*r*r/w;
    return std::isfinite(volume_weight)&&volume_weight>0.
        &&std::isfinite(angular_weight)&&angular_weight>0.;
}

/** One actual selected face, returned atomically through unpublished scratch.
 * Output arrays must not alias any scratch range; scratch bytes are always
 * provisional. Required B EOS errors propagate. Only documented trial EOS failures can
 * select the unchanged physical LLF baseline. Caller selects the genuine
 * 2:1 TVD policy BEFORE entering; no NG-based method substitution occurs here.
 */
template<class FluxPolicy,class ReconstructPolicy,class StateReader,class FractionReader,class Eos>
inline arch::state::Status compute(const StateReader& read,const FractionReader& fraction,
    const RzSelectedReconstruction::Context& context,const Eos& eos,double coefficient,
    const FluxAdmissibility::MeanThermoView* means,int left_cell,int right_cell,
    Scratch scratch,FluidVector& output,double* output_species) {
    using Status=arch::state::Status;
    const int species=context.species;
    if(species<0||(species&&(!scratch.x_left||!scratch.x_right
        ||!scratch.candidate_species||!scratch.high_sum||!scratch.low_sum||!output_species)))
        return Status::invalid_composition;
    const auto face=RzSelectedReconstruction::reconstruct_face<ReconstructPolicy>(
        read,fraction,context,eos,scratch.rhoX,scratch.reconstruction,scratch.reconstruction_count);
    if(face.status!=Status::valid)return face.status;
    FluidVector high_sum{},low_sum{};double theta=1.;
    for(int s=0;s<species;++s){scratch.high_sum[s]=0.;scratch.low_sum[s]=0.;}
    const int points=context.direction==0?1:4;
    for(int n=0;n<points;++n) {
        const int left_node=context.direction==0?1:4+n;
        const int right_node=context.direction==0?0:n;
        const auto& high_left=face.donor[0].point[left_node];
        const auto& high_right=face.donor[1].point[right_node];
        const auto& base_left=face.donor[0].baseline[left_node];
        const auto& base_right=face.donor[1].baseline[right_node];
        for(int s=0;s<species;++s) {
            if(!RzSelectedReconstruction::detail::quotient(
                scratch.rhoX[std::size_t(left_node)*species+s],high_left.rho,scratch.x_left[s])
                ||!RzSelectedReconstruction::detail::quotient(
                scratch.rhoX[std::size_t(8+right_node)*species+s],high_right.rho,scratch.x_right[s]))
                return Status::invalid_composition;
        }
        FluidVector high;
        FluxAdmissibility::compute_candidate([&] {
            FluxPolicy::compute_face_flux(high_left,high_right,scratch.x_left,scratch.x_right,
                species,eos,context.direction,coefficient,high,scratch.candidate_species,
                means,left_cell,right_cell);
        },high,scratch.candidate_species,species);
        for(int s=0;s<species;++s) {
            scratch.x_left[s]=fraction(s,left_cell);scratch.x_right[s]=fraction(s,right_cell);
        }
        double pl=0.,cl=0.,pr=0.,cr=0.;
        FluxAdmissibility::required_mean_thermo(base_left,scratch.x_left,eos,pl,cl);
        FluxAdmissibility::required_mean_thermo(base_right,scratch.x_right,eos,pr,cr);
        const auto factor=FluxAdmissibility::point_face_blend_with_thermo(
            base_left,base_right,scratch.x_left,scratch.x_right,species,
            pl,cl,pr,cr,context.direction,high,scratch.candidate_species);
        if(!factor.valid||!finite_flux(factor.low)||!std::isfinite(factor.theta)
            ||factor.theta<0.||factor.theta>1.)return Status::invalid_thermodynamics;
        theta=std::min(theta,factor.theta);
        double volume_weight=1.,angular_weight=1.;
        if(context.direction==1&&!axial_weights(context.geometry,context.face_i,
            face.donor[0].radius[left_node],n,volume_weight,angular_weight))
            return Status::invalid_thermodynamics;
        add_weighted(high_sum,high,volume_weight,angular_weight);
        add_weighted(low_sum,factor.low,volume_weight,angular_weight);
        const double mass_left=context.direction==0?base_left.mom_u:base_left.mom_v;
        const double mass_right=context.direction==0?base_right.mom_u:base_right.mom_v;
        for(int s=0;s<species;++s) {
            const double low_species=.5*mass_left*scratch.x_left[s]+.5*mass_right*scratch.x_right[s]
                -(.5*factor.wave_speed)*(base_right.rho*scratch.x_right[s]-base_left.rho*scratch.x_left[s]);
            if(!std::isfinite(low_species))return Status::invalid_composition;
            scratch.high_sum[s]+=volume_weight*scratch.candidate_species[s];
            scratch.low_sum[s]+=volume_weight*low_species;
        }
    }
    const auto final=theta==0.?low_sum:theta==1.?high_sum:low_sum+theta*(high_sum-low_sum);
    if(!finite_flux(final))return Status::nonfinite;
    for(int s=0;s<species;++s) {
        scratch.high_sum[s]=theta==0.?scratch.low_sum[s]:theta==1.?scratch.high_sum[s]:
            scratch.low_sum[s]+theta*(scratch.high_sum[s]-scratch.low_sum[s]);
        if(!std::isfinite(scratch.high_sum[s]))return Status::invalid_composition;
    }
    output=final;
    for(int s=0;s<species;++s)output_species[s]=scratch.high_sum[s];
    return Status::valid;
}
} // namespace RzNativeFaceFlux
