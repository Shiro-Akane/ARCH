/**
 * @file RzDensityMoments.h
 * @brief Shared density-only radial cell moments for the RZ reconstruction leaves.
 *
 * Workflow:
 * 1. Fit a density-only quadratic to three real same-row native V means and
 *    enforce positivity over the target cell while preserving its own V mean.
 *    A fitted negative ray contracts toward existing positive donor means;
 *    already-positive rays retain their original polynomial and arithmetic.
 *    Central support retains the original path; explicit one-sided support
 *    never fabricates a halo or infers an unavailable density.
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

/** Contract only a negative fit toward its positive native mean.
 * p_theta=rho_bar+theta*(p-rho_bar) preserves the V mean. The admissible
 * lower value d is the minimum of the same three positive donor means:
 * theta=(rho_bar-d)/(rho_bar-p_min). This avoids manufacturing a near-zero
 * physical point from positive donors at an outflow edge. It is a temporary
 * conservative ray limiter, not a change to any density mean or user floor.
 * c0 is recomputed from the actual moments; nextafter moves theta inward.
 * An already-positive polynomial keeps the original path. If rounding fails
 * the new contraction bound, the constant original mean is its valid limit.
 */
ARCH_INLINE bool positive_density(RzReconstruction::Polynomial& p,
    double mean,const RzReconstruction::Moments& moments,
    double left,double right,double donor_minimum,double& maximum)
{
    double minimum=0.;
    if(!(mean>0.)||!std::isfinite(mean)||!(donor_minimum>0.)||
       !std::isfinite(donor_minimum)||donor_minimum>mean||
       !extrema(p,left,right,minimum,maximum))return false;
    if(minimum>0.)return true;
    const double scale=std::fmax(mean,std::abs(minimum));
    double theta=(mean/scale-donor_minimum/scale)/(mean/scale-minimum/scale);
    if(theta>0.)theta=std::nextafter(theta,0.);
    if(!std::isfinite(theta)||theta<0.||theta>1.)return false;
    p.linear*=theta;p.quadratic*=theta;
    p.constant=mean-p.linear*moments.first-p.quadratic*moments.second;
    if(!extrema(p,left,right,minimum,maximum))return false;
    if(minimum<donor_minimum) {
        // The exact constant limit preserves the same mean and donor bound;
        // it does not alter evolved rho, its floor, or the common inertia rule.
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
 * Workflow: use the existing scaled quotient unchanged, then reject a nonzero
 * stored moment whose required coefficient rounds to zero. Returning NaN lets
 * existing closure/viscous finite guards reject instead of publishing lost J.
 * Representable omega and genuine zero m_phi retain their original arithmetic.
 * This coefficient-range guard does not implement a scaled physical profile or
 * admit an otherwise representable point state outside the current omega range.
 * The caller owns finite-m_phi/cell validation; no floor or momentum is changed.
 */
ARCH_INLINE double angular_velocity(double m_phi,const Cell& cell)
{
    const double numerator[]{m_phi,cell.weighted_two};
    const double denominator[]{cell.density_scale,cell.radius_scale,cell.weighted_three};
    const double omega=detail::scaled_value(numerator,2,denominator,3);
    if(m_phi!=0.&&omega==0.)return std::numeric_limits<double>::quiet_NaN();
    return omega;
}

/** Fit density on exactly three real same-row cells containing the target.
 * Four Gauss nodes exactly integrate quadratic*r^5 (degree seven). Radius and
 * density scaling preserve signed ghost capacity without forming r^5.
 * Only rho of the three native cells and the actual grid are read; E, momenta,
 * m_phi and composition do not enter these fixed graph moments and no EOS is
 * used. The explicit support consists of columns [support_begin,support_begin+2].
 * Invalid/missing support or density returns invalid, without extrapolation.
 * Target-anchor fit and p_theta=rho_target+theta*(p-rho_target) preserve the
 * target's actual V mean even when it is the first or last support cell.
 */
template<class StateReader>
ARCH_INLINE Cell density_cell_supported(const StateReader& read,int index,
    const GridMetrics::GeometryView& grid,int i,int support_begin)
{
    Cell result{};
    if(grid.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz||
       grid.geometry!=GridMetrics::Geometry::Cylindrical||grid.dim!=2||
       !std::isfinite(grid.dx1)||!(grid.dx1>0.)||grid.ng<1||grid.stride_y<3||
       i<0||i>=grid.stride_y||index<0||index>=grid.total_size||
       support_begin<0||support_begin>grid.stride_y-3||
       index%grid.stride_y!=i)return result;
    const int row_begin=index-i;
    const int support_end=support_begin+2;
    if(i<support_begin||i>support_end||support_end>=grid.total_size-row_begin)
        return result;
    for(int offset=0;offset<3;++offset) {
        const double low=grid.GetFacePosL(support_begin+offset),
            high=grid.GetFacePosR(support_begin+offset);
        if(!std::isfinite(low)||!std::isfinite(high)||!(high>low)||
           (low<0.&&high>0.))return result;
    }
    const int source=row_begin+support_begin;
    const auto low=read(source),middle=read(source+1),high=read(source+2);
    if(!std::isfinite(low.rho)||!(low.rho>0.)||
       !std::isfinite(middle.rho)||!(middle.rho>0.)||
       !std::isfinite(high.rho)||!(high.rho>0.))return result;
    const int target=i-support_begin;
    const bool central=target==1;
    RzReconstruction::RadialCell geometry{};
    if(central)geometry=RzReconstruction::radial_cell(grid,i);
    else {
        geometry.origin=grid.GetCellCenterX(i);geometry.spacing=grid.dx1;
        if(!std::isfinite(geometry.origin))return result;
        for(int n=0;n<3;++n)
            geometry.volume[n]=RzReconstruction::cell_moments(
                grid.GetFacePosL(support_begin+n),grid.GetFacePosR(support_begin+n),
                geometry.origin,geometry.spacing,false);
    }
    result.origin=geometry.origin;result.spacing=geometry.spacing;
    result.lower=grid.GetFacePosL(i);result.upper=grid.GetFacePosR(i);
    if(!std::isfinite(result.origin))return result;
    const double rho[]{low.rho,middle.rho,high.rho};
    const double target_mean=rho[target];
    if(central)
        result.density=RzReconstruction::field(low.rho,middle.rho,high.rho,geometry);
    else {
        // fit's middle observation is its authoritative anchor, not a
        // physical ordering requirement. Permute the same three real V
        // observations so c0=rho_target-c1*<t>_target-c2*<t^2>_target.
        const int left=target==0?1:0,right=target==2?1:2;
        result.density=RzReconstruction::fit(rho[left],target_mean,rho[right],
            geometry.volume[left],geometry.volume[target],geometry.volume[right]);
    }
    double density_scale=0.;
    if(!detail::positive_density(result.density,target_mean,geometry.volume[target],
        (result.lower-result.origin)/result.spacing,
        (result.upper-result.origin)/result.spacing,
        std::fmin(low.rho,std::fmin(middle.rho,high.rho)),density_scale))return result;
    density_scale=std::fmax(density_scale,target_mean);
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

/** Original centered density closure; preserve its three-cell arithmetic path.
 * A caller needing actual one-sided ghost support must select that explicit
 * support through density_cell_supported, never invent a missing halo.
 */
template<class StateReader>
ARCH_INLINE Cell density_cell(const StateReader& read,int index,
    const GridMetrics::GeometryView& grid,int i)
{
    if(i<1||index<1)return {};
    return density_cell_supported(read,index,grid,i,i-1);
}
} // namespace RzDensity
