/**
 * @file RzPolynomialMoments.h
 * @brief Low-level radial moment, polynomial-fit and Gauss leaf for full-ring RZ.
 *
 * Workflow:
 * 1. Bind one cell and its neighbors to their signed radial moment measures
 *    under the V measure (w=|r|) and the W measure (w=r^2).
 * 2. Fit a quadratic to three weighted cell means per field, and a cubic to
 *    four W-averaged means when a regular swirl would need a cubic.
 * 3. Expose the shared four-point Gauss nodes/weights that higher leaves use
 *    to integrate a quadratic density ray exactly.
 *
 * This leaf owns only the moment/quadratic/cubic/radial-cell/Gauss algebra.
 * Moments {first,second,third} are the first three dimensionless coordinate
 * moments under one cell measure: <t>=first, <t^2>=second, <t^3>=third for the
 * residual coordinate t=(r-origin)/spacing. one_side_moments integrates them
 * for a cell on one side of the axis; cell_moments splits a signed ghost
 * exactly at the axis. fit solves the three-coefficient quadratic from moment
 * differences; angular_field solves the four-coefficient cubic from four
 * W-averaged means; radial_cell caches the V and W measures of the stencil;
 * field selects one of the two measures; quadrature_node/quadrature_weight
 * return the shared four-point Gauss rule.
 *
 * It includes no density, flux, thermodynamics or stress header and stores no
 * conserved state, so no include cycle is introduced. Host/device callers
 * provide their own readers; all moment algebra is shared.
 */
#pragma once

#include <cmath>

#include "core/ArchPortability.h"
#include "grid/GridGeometryView.h"

namespace RzReconstruction {
/** First two dimensionless coordinate moments under one cell measure. */
struct Moments { double first=0.,second=0.,third=0.; };

/** Integrate moments for a cell that lies on one side of the radial axis.
 * For V, w=|r|; for W, w=r^2. With r=c+s, |s|<=d, the V displacement
 * moments are <s>=d^2/(3c), <s^2>=d^2/3. W uses
 * <s>=2*c*d^2/(3*c^2+d^2), <s^2>=(c^2*d^2/3+d^4/5)/(c^2+d^2/3).
 * Scaling c,d before squaring keeps thin, large-radius cells representable.
 */
ARCH_INLINE Moments one_side_moments(double left,double right,
    double origin,double spacing,bool angular)
{
    const double half=.5*(right-left),center=left+half;
    const double coordinate=(center-origin)/spacing;
    const double half_scaled=half/spacing;
    double shift=0.,square=0.,third=0.;
    if(angular) {
        const double scale=std::fmax(std::abs(center),half);
        const double c=center/scale,d=half/scale;
        const double denominator=c*c+d*d/3.;
        shift=(2.*c*d/3.)/denominator*half_scaled;
        square=(c*c/3.+d*d/5.)/denominator*half_scaled*half_scaled;
        third=(2.*c*d/5.)/denominator*half_scaled*half_scaled*half_scaled;
    } else {
        shift=half_scaled*half/(3.*center);
        square=half_scaled*half_scaled/3.;
        third=half_scaled*half_scaled*half_scaled*half/(5.*center);
    }
    return {coordinate+shift,coordinate*coordinate+2.*coordinate*shift+square,
        coordinate*coordinate*coordinate+3.*coordinate*coordinate*shift+3.*coordinate*square+third};
}

/** Integrate signed ghost-cell moments, splitting |r| at the axis if needed.
 * This is an exact moment operation, not an epsilon displacement of the axis.
 */
ARCH_INLINE Moments cell_moments(double left,double right,
    double origin,double spacing,bool angular)
{
    if(angular || left>=0. || right<=0.)
        return one_side_moments(left,right,origin,spacing,angular);
    const auto low=one_side_moments(left,0.,origin,spacing,false);
    const auto high=one_side_moments(0.,right,origin,spacing,false);
    const double scale=std::fmax(-left,right);
    const double l=left/scale,r=right/scale;
    const double low_fraction=l*l/(l*l+r*r);
    return {low_fraction*low.first+(1.-low_fraction)*high.first,
        low_fraction*low.second+(1.-low_fraction)*high.second,
        low_fraction*low.third+(1.-low_fraction)*high.third};
}

/** Three coefficients of p(t)=constant+linear*t+quadratic*t^2. */
struct Polynomial {
    double constant=0.,linear=0.,quadratic=0.;
    ARCH_INLINE double at(double coordinate) const {
        return constant+coordinate*(linear+coordinate*quadratic);
    }
};

/** Fit a quadratic to three weighted cell means; all three moments are exact.
 * Differences eliminate c0: dU_i=c1*d< t >_i+c2*d< t^2 >_i.
 * In particular, a linear physical swirl and quadratic total energy are
 * recovered exactly at the axis without recognizing an equilibrium model.
 */
ARCH_INLINE Polynomial fit(double left,double center,double right,
    const Moments& low,const Moments& middle,const Moments& high)
{
    const double a=low.first-middle.first,b=low.second-middle.second;
    const double c=high.first-middle.first,d=high.second-middle.second;
    const double determinant=a*d-b*c;
    const double linear=((left-center)*d-(right-center)*b)/determinant;
    const double quadratic=(a*(right-center)-c*(left-center))/determinant;
    return {center-linear*middle.first-quadratic*middle.second,linear,quadratic};
}

/** Stage-local polynomial geometry reused for fluid and composition fields. */
struct RadialCell {
    Moments volume[3],angular[4];
    double origin=0.,spacing=0.;
};

/** Bind the current cell and its neighbors to their signed radial measures. */
ARCH_INLINE RadialCell radial_cell(const GridMetrics::GeometryView& grid,int i)
{
    RadialCell result{};
    result.origin=grid.GetCellCenterX(i);result.spacing=grid.dx1;
    for(int n=0;n<3;++n) {
        const int cell=i+n-1;
        const double left=grid.GetFacePosL(cell),right=grid.GetFacePosR(cell);
        result.volume[n]=cell_moments(left,right,result.origin,result.spacing,false);
        result.angular[n]=cell_moments(left,right,result.origin,result.spacing,true);
    }
    result.angular[3]=cell_moments(grid.GetFacePosL(i+2),grid.GetFacePosR(i+2),
        result.origin,result.spacing,true);
    return result;
}

/** Fit one field under the selected authoritative mean measure. */
ARCH_INLINE Polynomial field(double left,double center,double right,
    const RadialCell& cell,bool angular=false)
{
    const auto* moments=angular?cell.angular:cell.volume;
    return fit(left,center,right,moments[0],moments[1],moments[2]);
}

/** Cubic angular polynomial; a regular linear/cubic swirl stays regular.
 * The extra moment is needed because rho*u_phi can be cubic even for rigid
 * rotation when density is quadratic. This is a coordinate stencil, not a
 * special rotation model or an extra stored fluid field.
 */
struct Cubic {
    double constant=0.,linear=0.,quadratic=0.,cubic=0.;
    ARCH_INLINE double at(double t) const {return constant+t*(linear+t*(quadratic+t*cubic));}
    /** Differentiate p(t); scale by t before large coefficients at t=0. */
    ARCH_INLINE double derivative(double t) const {
        return linear+quadratic*(2.*t)+cubic*((3.*t)*t);
    }
};

/** Solve the moment differences for one W-averaged cubic field. */
ARCH_INLINE Cubic angular_field(double low,double middle,double high,double extra,
    const RadialCell& cell)
{
    const auto& m=cell.angular[1];
    const double values[3]{low-middle,high-middle,extra-middle};
    const int rows[3]{0,2,3};
    double a[3][4]{};
    for(int row=0;row<3;++row) {
        const auto& n=cell.angular[rows[row]];
        a[row][0]=n.first-m.first;a[row][1]=n.second-m.second;
        a[row][2]=n.third-m.third;a[row][3]=values[row];
    }
    for(int col=0;col<3;++col) {
        int pivot=col;
        for(int row=col+1;row<3;++row)
            if(std::abs(a[row][col])>std::abs(a[pivot][col]))pivot=row;
        if(pivot!=col)for(int n=col;n<4;++n) {
            const double tmp=a[col][n];a[col][n]=a[pivot][n];a[pivot][n]=tmp;
        }
        for(int row=col+1;row<3;++row) {
            const double factor=a[row][col]/a[col][col];
            for(int n=col;n<4;++n)a[row][n]-=factor*a[col][n];
        }
    }
    const double c3=a[2][3]/a[2][2];
    const double c2=(a[1][3]-a[1][2]*c3)/a[1][1];
    const double c1=(a[0][3]-a[0][1]*c2-a[0][2]*c3)/a[0][0];
    return {middle-c1*m.first-c2*m.second-c3*m.third,c1,c2,c3};
}

/** Shared four-point Gauss nodes/weights, exact for degree-seven integrands. */
ARCH_INLINE double quadrature_node(int n) {
    constexpr double nodes[4]{-.86113631159405257522,-.33998104358485626480,
        .33998104358485626480,.86113631159405257522};return nodes[n];
}
ARCH_INLINE double quadrature_weight(int n) {
    constexpr double weights[4]{.34785484513745385737,.65214515486254614263,
        .65214515486254614263,.34785484513745385737};return weights[n];
}

} // namespace RzReconstruction
