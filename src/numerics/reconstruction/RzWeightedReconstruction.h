/**
 * @file RzWeightedReconstruction.h
 * @brief Selected conservative scalar MUSCL/PPM profiles on actual RZ cells.
 *
 * Workflow:
 * 1. Bind real signed one-side cells to their V=|r| or W=r^2 moments.
 * 2. MUSCL evaluates the selected Limiter::calc on weighted centroid gradients.
 * 3. PPM gathers six means, fits four-mean cubic faces with the shared solver,
 *    projects them with actual quadratic curvature support, and limits both
 *    endpoints of each donor using its weighted parabolic/CW moments.
 * 4. Return each donor's own-xi polynomial with its original mean. No state,
 *    EOS, composition, source, family acceptance or publication is owned here.
 *
 * This Native conservative specialization is distinct from ordinary primitive
 * EOS PPM. It grants no full-method order/TVD or whole-stencil qualification.
 */
#pragma once

#include <array>
#include <cmath>
#include <limits>

#include "core/ArchPortability.h"
#include "numerics/reconstruction/RzPolynomialMoments.h"

namespace RzReconstruction::weighted {
/** Existing algorithm coefficient, independent of physical units/config. */
inline constexpr double curvature_limit_coefficient=1.25;
/** Actual scalar cell and normalized xi moments; never a synthetic halo. */
struct Cell {
    double lower=0.,upper=0.;
    Moments xi{};
    bool angular=false,valid=false;
};

/** One conservative scalar profile in xi=(r-lower)/(upper-lower). */
struct Profile {
    Polynomial polynomial{};
    double lower=0.,upper=0.,mean=0.;
    bool valid=false;
    ARCH_INLINE double at(double radius) const {
        if(!valid||!std::isfinite(radius)||radius<lower||radius>upper)
            return std::numeric_limits<double>::quiet_NaN();
        return polynomial.at((radius-lower)/(upper-lower));
    }
};

/** Validate existing moment output without new epsilon/repair thresholds. */
ARCH_INLINE bool finite_moments(const Moments& m) {
    return std::isfinite(m.first)&&std::isfinite(m.second)&&std::isfinite(m.third);
}

/** Borrow actual one-sided support; whole-negative reflected cells are valid.
 * A cell crossing the axis is outside this selected kernel's contract. V uses
 * |r| and W uses r^2 through the existing shared cell_moments implementation.
 */
ARCH_INLINE bool bind_cell(double lower,double upper,bool angular,Cell& output) {
    if(!std::isfinite(lower)||!std::isfinite(upper)||!(upper>lower)
        ||(lower<0.&&upper>0.))return false;
    const double width=upper-lower;
    if(!std::isfinite(width)||!(width>0.))return false;
    const auto m=cell_moments(lower,upper,lower,width,angular);
    const double d=m.first-m.second,opposite=1.-2.*m.first+m.second;
    if(!finite_moments(m)||!(m.first>0.)||!(m.first<1.)
        ||!(m.second>0.)||!(d>0.)||!(opposite>0.))return false;
    output={lower,upper,m,angular,true};return true;
}

/** Validate actual contiguous support, its measure and all finite means. */
template<std::size_t N>
ARCH_INLINE bool valid_stencil(const std::array<double,N>& values,
    const std::array<Cell,N>& cells) {
    for(std::size_t n=0;n<N;++n) {
        Cell checked;
        if(!std::isfinite(values[n])||!cells[n].valid
            ||!bind_cell(cells[n].lower,cells[n].upper,cells[n].angular,checked)
            ||checked.xi.first!=cells[n].xi.first||checked.xi.second!=cells[n].xi.second
            ||checked.xi.third!=cells[n].xi.third
            ||cells[n].angular!=cells[0].angular
            ||(n&&cells[n-1].upper!=cells[n].lower))return false;
    }
    return true;
}

/** Reuse true weighted moments in a common residual coordinate. */
ARCH_INLINE bool moments_at(const Cell& cell,double origin,double spacing,Moments& output) {
    if(!cell.valid||!std::isfinite(origin)||!std::isfinite(spacing)||!(spacing>0.))return false;
    output=cell_moments(cell.lower,cell.upper,origin,spacing,cell.angular);
    return finite_moments(output);
}

/** Publish a finite polynomial only after computing its own conserved constant.
 * q(xi)=c0+c1*xi+c2*xi^2, c0=qbar-c1*mu1-c2*mu2.
 */
ARCH_INLINE bool make_profile(const Cell& cell,double mean,double linear,double quadratic,
    Profile& output) {
    const double constant=mean-linear*cell.xi.first-quadratic*cell.xi.second;
    if(!cell.valid||!std::isfinite(mean)||!std::isfinite(constant)
        ||!std::isfinite(linear)||!std::isfinite(quadratic))return false;
    Profile candidate{{constant,linear,quadratic},cell.lower,cell.upper,mean,true};
    if(!std::isfinite(candidate.polynomial.at(0.))
        ||!std::isfinite(candidate.polynomial.at(1.)))return false;
    output=candidate;return true;
}

/** Selected MUSCL slope from true centroid gradients; zero/opposite slopes
 * retain the original monotone branch. The ratio is dimensionless, so using
 * the donor's xi coordinate cancels the common physical spacing exactly.
 */
template<class Limiter>
ARCH_INLINE bool muscl_donor(const double* value,const Cell* cell,Profile& output) {
    const auto& donor=cell[1];const double width=donor.upper-donor.lower;
    Moments low,middle,high;
    if(!moments_at(cell[0],donor.lower,width,low)
        ||!moments_at(donor,donor.lower,width,middle)
        ||!moments_at(cell[2],donor.lower,width,high))return false;
    const double dl=middle.first-low.first,dr=high.first-middle.first;
    if(!std::isfinite(dl)||!std::isfinite(dr)||!(dl>0.)||!(dr>0.))return false;
    const double backward=(value[1]-value[0])/dl,forward=(value[2]-value[1])/dr;
    if(!std::isfinite(backward)||!std::isfinite(forward))return false;
    double slope=0.;
    if(forward!=0.&&(backward>0.)==(forward>0.)) {
        const double ratio=backward/forward;
        if(!std::isfinite(ratio))return false;
        const double phi=Limiter::calc(ratio);
        if(!std::isfinite(phi))return false;
        slope=phi*forward;
    }
    return make_profile(donor,value[1],slope,0.,output);
}

/** Return BOTH full donor profiles from the original four-cell MUSCL gather. */
template<class Limiter>
ARCH_INLINE bool muscl(const std::array<double,4>& values,const std::array<Cell,4>& cells,
    Profile& left,Profile& right) {
    if(!valid_stencil(values,cells))return false;
    Profile a,b;
    if(!muscl_donor<Limiter>(values.data(),cells.data(),a)
        ||!muscl_donor<Limiter>(values.data()+1,cells.data()+1,b))return false;
    left=a;right=b;return true;
}

/** Four weighted means determine one cubic; differences eliminate c0 using
 * the existing shared 3x3 pivot solve. No independent matrix solver is added.
 */
ARCH_INLINE bool cubic_face(const double* value,const Cell* cell,double origin,
    double spacing,double& face) {
    Moments m[4];for(int n=0;n<4;++n)if(!moments_at(cell[n],origin,spacing,m[n]))return false;
    const int rows[]{0,2,3};const double values[]{value[0]-value[1],value[2]-value[1],value[3]-value[1]};
    double a[3][4]{};
    for(int row=0;row<3;++row) {
        const auto& n=m[rows[row]];
        a[row][0]=n.first-m[1].first;a[row][1]=n.second-m[1].second;
        a[row][2]=n.third-m[1].third;a[row][3]=values[row];
    }
    double c1=0.,c2=0.,c3=0.;
    if(!solve_cubic_differences(a,c1,c2,c3))return false;
    face=value[1]-c1*m[1].first-c2*m[1].second-c3*m[1].third;
    return std::isfinite(face);
}

/** True quadratic curvature in the caller's common scaled coordinate.
 * The existing three-mean fit is reused; singular/nonfinite results reject.
 */
ARCH_INLINE bool curvature(const double* value,const Cell* cell,double origin,
    double spacing,double& output) {
    Moments m[3];for(int n=0;n<3;++n)if(!moments_at(cell[n],origin,spacing,m[n]))return false;
    const auto p=fit(value[0],value[1],value[2],m[0],m[1],m[2]);
    output=2.*p.quadratic;
    return std::isfinite(p.constant)&&std::isfinite(p.linear)&&std::isfinite(output);
}

/** Geometry-supported continuous PPM face projection. For a quadratic,
 * q_face=linear(mean_L,mean_R)-q''*C2/2, where C2 is the positive weighted
 * second moment about the actual face. Uniform p=0 recovers the original 1/6.
 */
ARCH_INLINE bool project_face(const double* value,const Cell* cell,double& output) {
    const double origin=cell[1].upper,spacing=cell[1].upper-cell[1].lower;
    double trial=0.,dl=0.,dr=0.;Moments left,right;
    if(!cubic_face(value,cell,origin,spacing,trial)
        ||!curvature(value,cell,origin,spacing,dl)
        ||!curvature(value+1,cell+1,origin,spacing,dr)
        ||!moments_at(cell[1],origin,spacing,left)
        ||!moments_at(cell[2],origin,spacing,right)
        ||!(left.first<0.)||!(right.first>0.))return false;
    const double distance=right.first-left.first;
    const double c2=(right.first/distance)*left.second-(left.first/distance)*right.second;
    const double support_left=curvature_limit_coefficient*dl,support_right=curvature_limit_coefficient*dr;
    if(!std::isfinite(support_left)||!std::isfinite(support_right))return false;
    const double positive=std::fmax(0.,std::fmin(support_left,support_right));
    const double negative=std::fmax(0.,std::fmin(-support_left,-support_right));
    const double lower=std::fmin(value[1],value[2])-.5*positive*c2;
    const double upper=std::fmax(value[1],value[2])+.5*negative*c2;
    if(!std::isfinite(distance)||!(distance>0.)||!std::isfinite(c2)||c2<0.
        ||!std::isfinite(positive)||!std::isfinite(negative)
        ||!std::isfinite(lower)||!std::isfinite(upper)||upper<lower)return false;
    output=std::fmax(lower,std::fmin(trial,upper));return std::isfinite(output);
}

/** Weighted Colella/Woodward monotone endpoint fallback. Let D=mu1-mu2.
 * |A|<=|qR-qL| gives asymmetric bounds L/R<=(mu1+D)/(1-mu1-D)
 * and R/L<=(1-mu1+D)/(mu1-D); uniform means give the original two-to-one.
 */
ARCH_INLINE bool cw_bound(double& left,double& right,double mean,const Moments& m) {
    const double l=left-mean,r=right-mean,d=m.first-m.second;
    const double denominator_left=1.-m.first-d,denominator_right=m.first-d;
    if(!std::isfinite(l)||!std::isfinite(r)||!(denominator_left>0.)||!(denominator_right>0.))return false;
    if((l>0.&&r>0.)||(l<0.&&r<0.)) {left=mean;right=mean;return true;}
    const double factor_left=(m.first+d)/denominator_left;
    const double factor_right=(1.-m.first+d)/denominator_right;
    const double bound_left=factor_left*std::abs(r),bound_right=factor_right*std::abs(l);
    if(!std::isfinite(factor_left)||!std::isfinite(factor_right)
        ||!std::isfinite(bound_left)||!std::isfinite(bound_right))return false;
    if(std::abs(l)>=bound_left)left=mean-factor_left*r;
    else if(std::abs(r)>=bound_right)right=mean-factor_right*l;
    return std::isfinite(left)&&std::isfinite(right);
}

/** Limit one donor's BOTH endpoints with the original 1.25 supported-curvature
 * blend. q=qL+delta*xi+A*xi*(1-xi), A=(mean-qL-delta*mu1)/D.
 * Derivative curvature is -2*A in own xi. All neighboring curvature witnesses
 * use that same xi scale; there is no rounded physical h^2 division.
 */
ARCH_INLINE bool ppm_donor(const double* value,const Cell* cell,double left,double right,
    Profile& output) {
    const auto& donor=cell[2];const auto& m=donor.xi;
    const double width=donor.upper-donor.lower,delta=right-left,d=m.first-m.second;
    const double a=(value[2]-left-delta*m.first)/d,p=-2.*a;
    if(!std::isfinite(a)||!std::isfinite(p))return false;
    double cw_left=left,cw_right=right;
    if(!cw_bound(cw_left,cw_right,value[2],m))return false;
    if(p!=0.) {
        double curvatures[3];
        for(int n=0;n<3;++n)
            if(!curvature(value+n,cell+n,donor.lower,width,curvatures[n]))return false;
        const double sign=p>0.?1.:-1.;
        for(double& c:curvatures) {
            c=curvature_limit_coefficient*sign*c;
            if(!std::isfinite(c))return false;
        }
        const double support=std::fmax(0.,std::fmin(curvatures[0],std::fmin(curvatures[1],curvatures[2])));
        if(!std::isfinite(support))return false;
        const double theta=std::fmin(1.,support/std::abs(p));
        if(theta==0.) {left=cw_left;right=cw_right;}
        else if(theta!=1.) {left=cw_left+theta*(left-cw_left);right=cw_right+theta*(right-cw_right);}
    }
    const double final_delta=right-left,final_a=(value[2]-left-final_delta*m.first)/d;
    return make_profile(donor,value[2],final_delta+final_a,-final_a,output);
}

/** Return both full parabolic donor profiles from the SAME six-cell gather.
 * Three projected faces feed two five-cell curvature/CW donors. No profile or
 * partial output is published unless all required mathematics is finite.
 */
ARCH_INLINE bool ppm(const std::array<double,6>& values,const std::array<Cell,6>& cells,
    Profile& left,Profile& right) {
    if(!valid_stencil(values,cells))return false;
    double faces[3];
    for(int n=0;n<3;++n)if(!project_face(values.data()+n,cells.data()+n,faces[n]))return false;
    Profile a,b;
    if(!ppm_donor(values.data(),cells.data(),faces[0],faces[1],a)
        ||!ppm_donor(values.data()+1,cells.data()+1,faces[1],faces[2],b))return false;
    left=a;right=b;return true;
}
} // namespace RzReconstruction::weighted
