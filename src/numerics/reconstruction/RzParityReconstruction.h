/**
 * @file RzParityReconstruction.h
 * @brief Selected three-source MUSCL profiles in the axis-regular r^2 basis.
 *
 * Workflow:
 * 1. Validate three actual signed cells, ordered by increasing absolute radius.
 * 2. Map their own V/W means into one common scaled even/factored-odd basis.
 * 3. Form actual centroid gradients in the target's stable local eta coordinate.
 * 4. Apply the selected Limiter::calc and preserve the target's original mean.
 * 5. Publish a bounded finite profile; its caller owns axis identity, parity,
 *    EOS/composition bounds, common theta and all stage publication.
 *
 * Even q=qbar+s*(t_eta-mu). Odd q=(r/R)*(gbar+s*(t_eta-mu)),
 * gbar=qbar/(<r>_M/R), t_eta=(r^2-l^2)/(u^2-l^2).
 * The odd f measure is r*M: r^2 dr for V, r^3 dr for W. These own-measure
 * identities preserve means mathematically; they are not bitwise conservation,
 * a whole-stage invariant theorem or a certificate of arbitrary field parity.
 */
#pragma once

#include <array>
#include <cmath>
#include <limits>

#include "core/ArchPortability.h"

namespace RzReconstruction::parity {
/** Native scalar parity and its actual conserved averaging measure. */
enum class Measure { EvenV, OddV, OddW };
/** Genuine signed endpoints; each cell has lower<upper in signed r. */
struct Cell { double lower=0.,upper=0.; };

/** Own eta moment and positive <|r|>/R, before the actual sign is applied. */
struct Moments { double coordinate=0.,radius=0.; };

namespace detail {
/** Accept only explicitly supported native representations. */
ARCH_INLINE bool known_measure(Measure measure) {
    return measure==Measure::EvenV||measure==Measure::OddV||measure==Measure::OddW;
}
/** A nonzero required factor may not silently become zero or nonfinite. */
ARCH_INLINE bool represented_product(double a,double b,double& output) {
    output=a*b;
    return std::isfinite(output)&&(!(a!=0.&&b!=0.)||output!=0.);
}
/** Normalize a native odd mean without constructing dimensional f=qbar/<r>. */
ARCH_INLINE bool represented_quotient(double a,double b,double& output) {
    if(!std::isfinite(a)||!std::isfinite(b)||b==0.)return false;
    output=a/b;
    return std::isfinite(output)&&(a==0.||output!=0.);
}
/** Actual positive mapped support; a zero-crossing cell is never synthesized. */
ARCH_INLINE bool map_cell(const Cell& cell,int& sign,double& lower,double& upper) {
    if(!std::isfinite(cell.lower)||!std::isfinite(cell.upper)||!(cell.upper>cell.lower))return false;
    if(cell.lower>=0.) {sign=1;lower=cell.lower;upper=cell.upper;}
    else if(cell.upper<=0.) {sign=-1;lower=-cell.upper;upper=-cell.lower;}
    else return false;
    return std::isfinite(lower)&&std::isfinite(upper)&&lower>=0.&&upper>lower;
}
/** Accepted independent own-measure moments, evaluated in a bounded ratio.
 * Set a=l/u,d=(u-l)/u to avoid cubic powers of tiny common-R coordinates.
 * OddV mu=[a^3+5a^2d/3+ad^2+d^3/5]/[(2a+d)(a^2+ad+d^2/3)].
 * OddW mu=(a^2+2)/(3(a^2+1)); EvenV mu=1/2.
 */
ARCH_INLINE bool own_moments(double lower,double upper,double scale,Measure measure,Moments& output) {
    if(!known_measure(measure)||!std::isfinite(scale)||!(scale>0.)
        ||!std::isfinite(lower)||!std::isfinite(upper)||lower<0.||!(upper>lower))return false;
    Moments result;
    if(measure==Measure::EvenV)result.coordinate=.5;
    else {
        const double a=lower/upper,d=(upper-lower)/upper,b=upper/scale;
        if(!std::isfinite(a)||!std::isfinite(d)||!(d>0.)||!std::isfinite(b)||!(b>0.))return false;
        if(measure==Measure::OddW) {
            result.coordinate=(a*a+2.)/(3.*(a*a+1.));
            result.radius=b*(.75*(a+1.)*(a*a+1.)/(a*a+a+1.));
        } else {
            const double numerator=a*a*a+(5./3.)*a*a*d+a*d*d+d*d*d/5.;
            const double denominator=(2.*a+d)*(a*a+a*d+d*d/3.);
            result.coordinate=numerator/denominator;
            result.radius=b*((2./3.)*(a*a+a+1.)/(a+1.));
        }
        if(!std::isfinite(result.radius)||!(result.radius>0.))return false;
    }
    if(!std::isfinite(result.coordinate)||!(result.coordinate>0.)||!(result.coordinate<1.))return false;
    output=result;return true;
}
/** Map a source's own eta moment into the target's eta coordinate.
 * Factoring every square difference gives
 * (a-l)/D*(a+l)/(u+l)+mu_source*(b-a)/D*(b+a)/(u+l).
 * No global r^2 or nearly equal global eta centroids are subtracted.
 */
ARCH_INLINE bool mapped_coordinate(double lower,double upper,double own_coordinate,
    double target_lower,double target_upper,double scale,double& output) {
    const double width=target_upper-target_lower;
    const double denominator=target_upper/scale+target_lower/scale;
    if(!std::isfinite(width)||!(width>0.)||!std::isfinite(denominator)||!(denominator>0.))return false;
    const double offset=(lower-target_lower)/width,span=(upper-lower)/width;
    const double lower_ratio=(lower/scale+target_lower/scale)/denominator;
    const double span_ratio=(upper/scale+lower/scale)/denominator;
    if(!std::isfinite(offset)||!std::isfinite(span)||!(span>0.)
        ||!std::isfinite(lower_ratio)||!std::isfinite(span_ratio))return false;
    output=offset*lower_ratio+own_coordinate*span*span_ratio;
    return std::isfinite(output);
}
} // namespace detail

/** Bounded physical polynomial with one common support scale R.
 * mean is the untouched target native mean; factor_mean is qbar for even
 * fields and gbar=R*fbar for odd fields. Its original measure determines mu.
 */
struct Profile {
    Cell cell{};
    double absolute_lower=0.,absolute_upper=0.,radius_scale=0.;
    double mean=0.,factor_mean=0.,slope=0.,mean_coordinate=0.;
    Measure measure=Measure::EvenV;
    bool valid=false;

    /** Evaluate actual signed r within this genuine donor, without EOS/floors.
     * An exact polynomial zero is legitimate; a nonzero factor product that
     * rounds to zero is rejected. The physical axis has exact odd value zero.
     */
    ARCH_INLINE double at(double radius) const {
        const double bad=std::numeric_limits<double>::quiet_NaN();
        if(!valid||!detail::known_measure(measure)||!std::isfinite(radius)
            ||radius<cell.lower||radius>cell.upper)return bad;
        const double r=std::abs(radius),width=absolute_upper-absolute_lower;
        const double denominator=absolute_upper/radius_scale+absolute_lower/radius_scale;
        if(!std::isfinite(width)||!(width>0.)||!std::isfinite(denominator)||!(denominator>0.))return bad;
        const double coordinate=(r-absolute_lower)/width
            *((r/radius_scale+absolute_lower/radius_scale)/denominator);
        const double value=factor_mean+slope*(coordinate-mean_coordinate);
        if(!std::isfinite(coordinate)||!std::isfinite(value))return bad;
        if(measure==Measure::EvenV)return value;
        if(radius==0.)return 0.;
        const double scaled=radius/radius_scale;
        if(!std::isfinite(scaled)||scaled==0.)return bad;
        double result=0.;
        return detail::represented_product(scaled,value,result)?result:bad;
    }
};

/** Reconstruct one true three-source MUSCL donor, publishing only on success.
 * target0 is the genuine first positive/negative reflected physical cell;
 * target1 is the genuine centered cell. The caller authenticates why that
 * support is legal. No source parity or global axis policy is guessed here.
 * MC/TVD ratio branch is exactly phi(backward/forward)*forward; an opposite
 * sign or zero forward gradient retains the existing zero-slope branch.
 */
template<class Limiter>
ARCH_INLINE bool reconstruct(const std::array<double,3>& means,
    const std::array<Cell,3>& signed_cells,int target,Measure measure,Profile& output) {
    if((target!=0&&target!=1)||!detail::known_measure(measure))return false;
    double lower[3],upper[3],value[3],coordinate[3];int signs[3];
    for(int n=0;n<3;++n) {
        if(!std::isfinite(means[n])||!detail::map_cell(signed_cells[n],signs[n],lower[n],upper[n])
            ||(n&&(signs[n]!=signs[0]||lower[n]!=upper[n-1])))return false;
    }
    const double scale=upper[2];
    if(!std::isfinite(scale)||!(scale>0.))return false;
    Moments moments[3];
    for(int n=0;n<3;++n) {
        if(!detail::own_moments(lower[n],upper[n],scale,measure,moments[n]))return false;
        if(measure==Measure::EvenV)value[n]=means[n];
        else if(!detail::represented_quotient(means[n],signs[n]*moments[n].radius,value[n]))return false;
        if(n==target)coordinate[n]=moments[n].coordinate;
        else if(!detail::mapped_coordinate(lower[n],upper[n],moments[n].coordinate,
            lower[target],upper[target],scale,coordinate[n]))return false;
    }
    const double dl=coordinate[1]-coordinate[0],dr=coordinate[2]-coordinate[1];
    if(!std::isfinite(dl)||!std::isfinite(dr)||!(dl>0.)||!(dr>0.))return false;
    const double backward=(value[1]-value[0])/dl,forward=(value[2]-value[1])/dr;
    if(!std::isfinite(backward)||!std::isfinite(forward))return false;
    double slope=0.;
    if(forward!=0.&&(backward>0.)==(forward>0.)) {
        double ratio=0.;
        if(!detail::represented_quotient(backward,forward,ratio))return false;
        const double phi=Limiter::calc(ratio);
        if(!std::isfinite(phi)||!detail::represented_product(phi,forward,slope))return false;
    }
    if(!std::isfinite(slope))return false;
    Profile result;
    result.cell=signed_cells[target];result.absolute_lower=lower[target];result.absolute_upper=upper[target];
    result.radius_scale=scale;result.mean=means[target];result.factor_mean=value[target];
    result.slope=slope;result.mean_coordinate=moments[target].coordinate;
    result.measure=measure;result.valid=true;
    // Finite factor endpoints are required even at the axis, whose final odd
    // product is zero. They must not conceal a nonfinite polynomial amplitude.
    if(!std::isfinite(result.factor_mean-result.slope*result.mean_coordinate)
        ||!std::isfinite(result.factor_mean+result.slope*(1.-result.mean_coordinate))
        ||!std::isfinite(result.at(result.cell.lower))||!std::isfinite(result.at(result.cell.upper)))return false;
    output=result;return true;
}
} // namespace RzReconstruction::parity
