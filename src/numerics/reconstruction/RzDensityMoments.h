/**
 * @file RzDensityMoments.h
 * @brief Shared density-only radial cell moments for the RZ reconstruction leaves.
 *
 * Workflow:
 * 1. Fit a density-only quadratic to three native V means and enforce its
 *    positivity over the whole cell while preserving the current V mean.
 * 2. Integrate C=int rho*r^3 dr and s=int rho*r^5 dr/C with four Gauss points,
 *    and cache the normalized quadrature factors an angular caller needs.
 * 3. Expose angular_velocity, which forms temporary omega=J/(2*pi*dz*C) from
 *    the sole stored m_phi=J/W on demand.
 *
 * Formulas: density_cell fits the ray p(t)=c0+c1*t+c2*t^2, c0=rho_bar-c1*<t>_V-c2*<t^2>_V in residual
 * coordinate t=(r-origin)/spacing by calling the shared low-level leaf; the
 * positivity contraction is p_theta=rho_bar+theta*(p-rho_bar) with c0
 * recomputed from the exact authoritative moments; capacity is
 * C=half*density_scale*radius_scale^3*weighted_three and the graph abscissa is
 * s=radius_scale^2*weighted_five/weighted_three; angular_velocity is
 * m_phi*weighted_two/(density_scale*radius_scale*weighted_three). The scaling
 * c,d before squaring keeps thin, large-radius and signed reflected ghost
 * cells representable without ever forming r^5.
 *
 * Density is a temporary numerical polynomial here, not a second conserved
 * state. density_cell reads only rho of the three native cells and the actual
 * grid: no E, no momentum, no m_phi, no composition, no EOS and no evolved
 * angular array are read or validated. Callers provide their own reader.
 * Includes stay below this leaf (core, grid, shared moment leaf) so there is no
 * include cycle, and omega arithmetic is retained unchanged for the angular
 * owner that consumes this cell.
 */
#pragma once

#include <cmath>
#include <limits>

#include "core/ArchPortability.h"
#include "grid/GridGeometryView.h"
#include "numerics/reconstruction/RzPolynomialMoments.h"

namespace RzDensity {
/** Density-only radial cell moments; no new conserved state and no omega.
 * density is p(t), t=(r-origin)/spacing. C=int rho*r^3 dr is signed in a
 * wholly negative reflected ghost, while s=int rho*r^5 dr/C stays positive.
 * density_scale/radius_scale/weighted_two/weighted_three are the normalized
 * quadrature data an angular caller needs to form omega without re-integrating.
 */
struct Cell {
    RzReconstruction::Polynomial density;
    double origin=0.,spacing=0.,lower=0.,upper=0.;
    double capacity=0.,mean_s=0.;
    bool valid=false;
    double density_scale=0.,radius_scale=0.,weighted_two=0.,weighted_three=0.;
};

namespace detail {
/** Exponent-scaled product/quotient avoids intermediate h*r^3*rho overflow.
 * Only an unrepresentable final result remains nonfinite/zero for rejection.
 */
ARCH_INLINE double scaled_value(const double* numerator,int count,
    const double* denominator=nullptr,int divisors=0)
{
    double mantissa=1.;int exponent=0;bool zero=false;
    for(int side=0;side<2;++side) {
        const double* factors=side==0?numerator:denominator;
        const int size=side==0?count:divisors;
        for(int n=0;n<size;++n) {
            const double value=factors[n];
            if(!std::isfinite(value)||(side==1&&value==0.))
                return std::numeric_limits<double>::quiet_NaN();
            if(value==0.) {zero=true;continue;}
            int power=0;const double fraction=std::frexp(value,&power);
            if(side==0) {mantissa*=fraction;exponent+=power;}
            else {mantissa/=fraction;exponent-=power;}
        }
    }
    return zero?0.:std::ldexp(mantissa,exponent);
}

/** Exact quadratic extrema at both endpoints and an interior vertex. */
ARCH_INLINE bool extrema(const RzReconstruction::Polynomial& p,
    double left,double right,double& minimum,double& maximum)
{
    if(!std::isfinite(p.constant)||!std::isfinite(p.linear)||
       !std::isfinite(p.quadratic)||!std::isfinite(left)||
       !std::isfinite(right)||!(right>left))return false;
    const double low=p.at(left),high=p.at(right);
    if(!std::isfinite(low)||!std::isfinite(high))return false;
    minimum=std::fmin(low,high);maximum=std::fmax(low,high);
    if(p.quadratic!=0.) {
        const double vertex=(-.5*p.linear)/p.quadratic;
        if(vertex>left&&vertex<right) {
            const double value=p.at(vertex);
            if(!std::isfinite(value))return false;
            minimum=std::fmin(minimum,value);maximum=std::fmax(maximum,value);
        }
    }
    return true;
}

/** Contract p toward its positive native mean without a dimensional floor.
 * p_theta=rho_bar+theta*(p-rho_bar) preserves the V mean. c0 is recomputed
 * from the exact authoritative moments after contraction. nextafter moves
 * the positivity boundary inward; theta=0 is the constant limiting segment.
 */
ARCH_INLINE bool positive_density(RzReconstruction::Polynomial& p,
    double mean,const RzReconstruction::Moments& moments,
    double left,double right,double& maximum)
{
    double minimum=0.;
    if(!(mean>0.)||!std::isfinite(mean)||
       !extrema(p,left,right,minimum,maximum))return false;
    if(minimum>0.)return true;
    const double scale=std::fmax(mean,std::abs(minimum));
    double theta=(mean/scale)/(mean/scale-minimum/scale);
    theta=std::nextafter(theta,0.);
    if(!std::isfinite(theta)||theta<0.||theta>1.)return false;
    p.linear*=theta;p.quadratic*=theta;
    p.constant=mean-p.linear*moments.first-p.quadratic*moments.second;
    if(!extrema(p,left,right,minimum,maximum))return false;
    if(!(minimum>0.)) {
        // Rounding can leave a zero endpoint. The constant mean keeps the
        // same strictly positive integral without introducing a physical floor.
        p={mean,0.,0.};maximum=mean;
    }
    return true;
}

/** Validate the signed density cell and its density, including an interior
 * minimum. Omega is deliberately absent here: the angular owner adds its own
 * finite-m_phi/omega check on top of this density-only validity.
 */
ARCH_INLINE bool cell_valid(const Cell& cell)
{
    if(!cell.valid||!std::isfinite(cell.origin)||
       !std::isfinite(cell.spacing)||!(cell.spacing>0.)||
       !std::isfinite(cell.lower)||!std::isfinite(cell.upper)||
       !(cell.upper>cell.lower)||(cell.lower<0.&&cell.upper>0.)||
       !std::isfinite(cell.capacity)||
       !std::isfinite(cell.mean_s)||!(cell.mean_s>0.)||
       (cell.lower>=0.?!(cell.capacity>0.):!(cell.capacity<0.)))return false;
    double minimum=0.,maximum=0.;
    return extrema(cell.density,(cell.lower-cell.origin)/cell.spacing,
        (cell.upper-cell.origin)/cell.spacing,minimum,maximum)&&minimum>0.;
}
} // namespace detail

/** Temporary omega=J/(2*pi*dz*C) from the sole stored m_phi=J/W.
 * Reuses the same exponent-scaled product/quotient as the original angular
 * cell, so rounding and finiteness behavior are unchanged. The caller owns the
 * finite-m_phi policy: this leaf never reads a momentum state.
 */
ARCH_INLINE double angular_velocity(double m_phi,const Cell& cell)
{
    const double numerator[]{m_phi,cell.weighted_two};
    const double denominator[]{cell.density_scale,cell.radius_scale,cell.weighted_three};
    return detail::scaled_value(numerator,2,denominator,3);
}

/** Fit density alone and integrate C and its r^2 graph abscissa.
 * Four Gauss nodes exactly integrate quadratic*r^5 (degree seven). Radius and
 * density scaling preserve signed ghost capacity without forming r^5.
 * Only rho of the three native cells and the actual grid are read; E, momenta,
 * m_phi and composition do not enter these fixed graph moments and no EOS is
 * used. Invalid geometry, nonpositive rho or a nonpositive ray returns an
 * invalid cell through the existing guards.
 */
template<class StateReader>
ARCH_INLINE Cell density_cell(const StateReader& read,int index,
    const GridMetrics::GeometryView& grid,int i)
{
    Cell result{};
    if(grid.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz||
       grid.geometry!=GridMetrics::Geometry::Cylindrical||grid.dim!=2||
       !std::isfinite(grid.dx1)||!(grid.dx1>0.)||grid.ng<1||grid.stride_y<=0||
       i<1||i+1>=grid.stride_y||index<1||index+1>=grid.total_size||
       index%grid.stride_y!=i)return result;
    for(int offset=-1;offset<=1;++offset) {
        const double low=grid.GetFacePosL(i+offset),high=grid.GetFacePosR(i+offset);
        if(!std::isfinite(low)||!std::isfinite(high)||!(high>low)||
           (low<0.&&high>0.))return result;
    }
    const auto low=read(index-1),middle=read(index),high=read(index+1);
    if(!std::isfinite(low.rho)||!(low.rho>0.)||
       !std::isfinite(middle.rho)||!(middle.rho>0.)||
       !std::isfinite(high.rho)||!(high.rho>0.))return result;
    const auto geometry=RzReconstruction::radial_cell(grid,i);
    result.origin=geometry.origin;result.spacing=geometry.spacing;
    result.lower=grid.GetFacePosL(i);result.upper=grid.GetFacePosR(i);
    if(!std::isfinite(result.origin))return result;
    result.density=RzReconstruction::field(low.rho,middle.rho,high.rho,geometry);
    double density_scale=0.;
    if(!detail::positive_density(result.density,middle.rho,geometry.volume[1],
        (result.lower-result.origin)/result.spacing,
        (result.upper-result.origin)/result.spacing,density_scale))return result;
    density_scale=std::fmax(density_scale,middle.rho);
    const double radius_scale=std::fmax(std::abs(result.lower),std::abs(result.upper));
    const double half=.5*(result.upper-result.lower),midpoint=result.lower+half;
    if(!(density_scale>0.)||!(radius_scale>0.)||!std::isfinite(half))return result;
    double weighted_three=0.,weighted_five=0.,weighted_two=0.;
    for(int n=0;n<4;++n) {
        const double radius=midpoint+half*RzReconstruction::quadrature_node(n);
        const double rho=result.density.at((radius-result.origin)/result.spacing);
        if(!std::isfinite(rho)||!(rho>0.))return result;
        const double r=radius/radius_scale,r2=r*r;
        const double weight=RzReconstruction::quadrature_weight(n);
        const double three=weight*(rho/density_scale)*r2*r;
        weighted_three+=three;weighted_five+=three*r2;weighted_two+=weight*r2;
    }
    const double capacity[]{half,density_scale,radius_scale,radius_scale,radius_scale,weighted_three};
    const double square_moment[]{radius_scale,radius_scale,weighted_five};
    const double third[]{weighted_three};
    result.capacity=detail::scaled_value(capacity,6);
    result.mean_s=detail::scaled_value(square_moment,3,third,1);
    result.density_scale=density_scale;result.radius_scale=radius_scale;
    result.weighted_two=weighted_two;result.weighted_three=weighted_three;
    result.valid=true;result.valid=detail::cell_valid(result);return result;
}
} // namespace RzDensity
