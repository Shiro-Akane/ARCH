// Focused independent checks of the shared PPM curvature limiter.
//
// The reference functions below independently transcribe the continuous
// extended-interval face projection and the
// curvature-supported CW blend, both with the single method coefficient
// C = 1.25 (Sekora/Colella 2009, arXiv:0903.4200 informs the curvature
// support). They call no PPM internals, so a mismatch means the production PPM
// face or profile formulas drifted from the frozen design. Cell averages are the
// reconstruction input; every tolerance is a floating-point rounding allowance
// and carries no physical units.
#include "numerics/reconstruction/Reconstruction.h"
#include "numerics/reconstruction/RzParityReconstruction.h"
#include "numerics/reconstruction/RzWeightedReconstruction.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

constexpr double kEps = std::numeric_limits<double>::epsilon();
constexpr double kCoefficient = 1.25;
constexpr double kCurvatureBand = 64.0;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

// Allow only accumulated rounding, never a different limiter branch.
void close_relative(double actual, double expected, double scale, const char* message)
{
    const double tolerance = 32.0 * kEps * std::max(scale, std::abs(expected));
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        std::cerr << message << ": " << actual << " vs " << expected
                  << " (tolerance " << tolerance << ")\n";
        throw std::runtime_error(message);
    }
}

double reference_interpolate_face(double u_im1, double u_i, double u_ip1, double u_ip2)
{
    return (7.0 / 12.0) * (u_i + u_ip1) - (1.0 / 12.0) * (u_im1 + u_ip2);
}

// Contract face projection: clamp the fourth-order face into the continuous
// extended interval [min - kp/6, max + kn/6] built from the positive/negative
// curvature supports. No binary inside/outside switch.
double reference_face(
    double f, double u_im1, double u_i, double u_ip1, double u_ip2)
{
    const double d_left = (u_ip1 - u_i) - (u_i - u_im1);
    const double d_right = (u_ip2 - u_ip1) - (u_ip1 - u_i);
    const double k_positive = std::max(0.0,
        std::min(kCoefficient * d_left, kCoefficient * d_right));
    const double k_negative = std::max(0.0,
        std::min(-kCoefficient * d_left, -kCoefficient * d_right));
    const double lower = std::min(u_i, u_ip1) - k_positive / 6.0;
    const double upper = std::max(u_i, u_ip1) + k_negative / 6.0;
    return std::max(lower, std::min(f, upper));
}

// Contract cell profile: the CW two-to-one bound is computed unconditionally,
// then blended back toward the original faces by the curvature-supported theta.
void reference_cell(double& fL, double& fR,
    double u_im2, double u_im1, double u_i, double u_ip1, double u_ip2)
{
    double cw_left = fL;
    double cw_right = fR;
    const double delta_left = cw_left - u_i;
    const double delta_right = cw_right - u_i;
    if ((delta_left > 0.0 && delta_right > 0.0)
        || (delta_left < 0.0 && delta_right < 0.0)) {
        cw_left = u_i;
        cw_right = u_i;
    } else if (std::abs(delta_left) >= 2.0 * std::abs(delta_right)) {
        cw_left = u_i - 2.0 * delta_right;
    } else if (std::abs(delta_right) >= 2.0 * std::abs(delta_left)) {
        cw_right = u_i - 2.0 * delta_left;
    }

    const double a_left = fL - u_i;
    const double a_right = fR - u_i;
    const double P = 6.0 * (a_left + a_right);
    if (P == 0.0)
        return; // exact linear profile: original faces already lie on it
    const double d_left = (u_i - u_im1) - (u_im1 - u_im2);
    const double d_center = (u_ip1 - u_i) - (u_i - u_im1);
    const double d_right = (u_ip2 - u_ip1) - (u_ip1 - u_i);
    const double S = P > 0.0 ? 1.0 : -1.0;
    const double k = std::max(0.0, std::min({kCoefficient * S * d_left,
        kCoefficient * S * d_center, kCoefficient * S * d_right}));
    const double theta = std::min(1.0, k / std::abs(P));
    if (theta == 1.0)
        return;
    if (theta == 0.0) {
        fL = cw_left;
        fR = cw_right;
        return;
    }
    fL = cw_left + theta * (fL - cw_left);
    fR = cw_right + theta * (fR - cw_right);
}

// v always points at the six cell averages v[0]..v[5] of one stencil.
void reference_ppm(const double* v, double& left, double& right)
{
    const double face_imhalf = reference_face(
        reference_interpolate_face(v[0], v[1], v[2], v[3]), v[0], v[1], v[2], v[3]);
    const double face_iphalf = reference_face(
        reference_interpolate_face(v[1], v[2], v[3], v[4]), v[1], v[2], v[3], v[4]);
    const double face_ip3half = reference_face(
        reference_interpolate_face(v[2], v[3], v[4], v[5]), v[2], v[3], v[4], v[5]);

    double left_cell = face_imhalf;
    double right_cell = face_iphalf;
    reference_cell(left_cell, right_cell, v[0], v[1], v[2], v[3], v[4]);

    double left_next = face_iphalf;
    double right_next = face_ip3half;
    reference_cell(left_next, right_next, v[1], v[2], v[3], v[4], v[5]);

    left = right_cell;
    right = left_next;
}

std::pair<double, double> production(const double* v)
{
    double left = 0.0;
    double right = 0.0;
    double stencil[6];
    for (int k = 0; k < 6; ++k) stencil[k] = v[k];
    PPMReconstruction::reconstruct_scalar_ppm(stencil, left, right);
    require(std::isfinite(left) && std::isfinite(right), "production face not finite");
    return {left, right};
}

double stencil_scale(const double* v)
{
    double scale = 0.0;
    for (int k = 0; k < 6; ++k) scale = std::max(scale, std::abs(v[k]));
    return scale;
}

void check_matches_reference(const double* v, const char* message)
{
    double expected_left = 0.0;
    double expected_right = 0.0;
    reference_ppm(v, expected_left, expected_right);
    const auto [left, right] = production(v);
    const double scale = stencil_scale(v);
    close_relative(left, expected_left, scale, message);
    close_relative(right, expected_right, scale, message);
}

// A constant field is flat in every cell, so both faces must return the
// constant exactly: no limiter may displace a zero-curvature profile.
void constants()
{
    for (double c : {1.0, -3.5, 1e-100, 1e100}) {
        double v[6] = {c, c, c, c, c, c};
        const auto [left, right] = production(v);
        require(left == c && right == c, "constant field must reconstruct exactly");
        check_matches_reference(v, "constant field reference");
    }
}

// Affine cell averages are reproduced at both interfaces by construction, and
// the CW endpoint bound must leave such a profile untouched.
void affine_fields()
{
    for (double slope : {0.5, -0.25, 3.0}) {
        for (double intercept : {2.0, -7.5}) {
            double v[6];
            for (int k = 0; k < 6; ++k) v[k] = intercept + slope * static_cast<double>(k - 2);
            const auto [left, right] = production(v);
            const double exact = 0.5 * (v[2] + v[3]);
            const double scale = std::max({std::abs(v[2]), std::abs(v[3]), std::abs(exact)});
            close_relative(left, exact, scale, "affine left interface");
            close_relative(right, exact, scale, "affine right interface");
            check_matches_reference(v, "affine reference");
        }
    }
}

// PPM is exact for a quadratic: with uniform cell means u_i = q(x_i) + c/12 the
// interface value is 0.5*(u_i+u_{i+1}) - c/3, and neither curvature limit may
// reduce the retained curvature below that exact parabola. Convex (c > 0) and
// concave (c < 0) profiles are both covered, including cells that straddle the
// analytic extremum.
void exact_quadratics()
{
    for (double c : {0.25, -0.5, 2.0}) {
        for (double b : {0.5, -1.25}) {
            for (int center = -3; center <= 3; ++center) {
                double v[6];
                for (int k = 0; k < 6; ++k) {
                    const double x = static_cast<double>(center - 2 + k);
                    v[k] = 2.0 + b * x + c * x * x + c / 12.0;
                }
                const auto [left, right] = production(v);
                const double exact = 0.5 * (v[2] + v[3]) - c / 3.0;
                const double scale = std::max(stencil_scale(v), std::abs(exact));
                close_relative(left, exact, scale, "quadratic left interface");
                close_relative(right, exact, scale, "quadratic right interface");
                check_matches_reference(v, "quadratic reference");
            }
        }
    }
}

// Two-ULP witness continuity: the old binary smooth-extremum bypass moved the
// left face by 0.0052083333333332593 for the documented input delta, because a
// two-ULP change flipped a boolean branch. The curvature-limited formulas must
// stay within 64 eps of the value scale for the same, the reversed and the
// rescaled witnesses, and for every single-entry two-ULP perturbation.
void witness_continuity()
{
    const double a = 1.0 / 32.0;
    const double forward[6] = {1.0 - 2.5 * a, 1.0 - a, 1.0, 1.0 - a, 1.0 - 4.0 * a, 1.0 - 9.0 * a};
    double reversed[6];
    for (int k = 0; k < 6; ++k) reversed[k] = forward[5 - k];

    const double* witnesses[2] = {forward, reversed};
    for (const double* witness : witnesses) {
        const auto [base_left, base_right] = production(witness);
        const double band = kCurvatureBand * kEps * stencil_scale(witness);
        check_matches_reference(witness, "witness stencil reference");

        // Documented witness: two nextafter steps of values[0] toward 1.
        double perturbed[6];
        for (int k = 0; k < 6; ++k) perturbed[k] = witness[k];
        perturbed[0] = std::nextafter(std::nextafter(perturbed[0], 1.0), 1.0);
        require(perturbed[0] - witness[0] == 2.2204460492503131e-16,
            "witness perturbation is not the documented two-ULP delta");
        const auto [left, right] = production(perturbed);
        require(std::abs(left - base_left) <= band, "witness left face jump exceeds 64 eps");
        require(std::abs(right - base_right) <= band, "witness right face jump exceeds 64 eps");
        // The retired bypass jump was 0.0052083333333332593 of the value scale.
        require(std::abs(left - base_left) < 1e-3 * stencil_scale(witness),
            "witness left face reproduced the retired bypass jump");

        // Every stencil entry, two nextafter steps toward 1 and toward 0.
        for (int entry = 0; entry < 6; ++entry) {
            for (double target : {1.0, 0.0}) {
                double single[6];
                for (int k = 0; k < 6; ++k) single[k] = witness[k];
                single[entry] = std::nextafter(std::nextafter(single[entry], target), target);
                const auto [left_one, right_one] = production(single);
                require(std::abs(left_one - base_left) <= band, "two-ULP entry scan left jump");
                require(std::abs(right_one - base_right) <= band, "two-ULP entry scan right jump");
            }
        }

        // Reversed/scaled witnesses: 1e-100 and 1e100 keep the relative shape.
        for (double factor : {1e-100, 1e100}) {
            double scaled[6];
            double scaled_perturbed[6];
            for (int k = 0; k < 6; ++k) {
                scaled[k] = witness[k] * factor;
                scaled_perturbed[k] = scaled[k];
            }
            scaled_perturbed[0] =
                std::nextafter(std::nextafter(scaled_perturbed[0], 1.0), 1.0);
            const auto [scaled_left, scaled_right] = production(scaled);
            const auto [scaled_left_one, scaled_right_one] = production(scaled_perturbed);
            const double scaled_band = kCurvatureBand * kEps * stencil_scale(scaled);
            require(std::abs(scaled_left_one - scaled_left) <= scaled_band,
                "scaled witness left face jump exceeds 64 eps");
            require(std::abs(scaled_right_one - scaled_right) <= scaled_band,
                "scaled witness right face jump exceeds 64 eps");
            // Scale invariance of the face formulas: faces scale with the data.
            close_relative(scaled_left / factor, base_left, stencil_scale(witness),
                "scaled witness left face not scale invariant");
            close_relative(scaled_right / factor, base_right, stencil_scale(witness),
                "scaled witness right face not scale invariant");
        }
    }
}

// These nonsmooth controls must retain finite faces inside their stencil range.
// This is not a bound on all smooth profiles: a quadratic's true extremum face
// can lie outside the range of its cell averages.
void discontinuity_controls()
{
    const double controls[][6] = {
        {1.0, 1.0, 1.0, 2.0, 2.0, 2.0},
        {2.0, 2.0, 2.0, 1.0, 1.0, 1.0},
        {1.0, 1.0, 1.0, 1.5, 1.0, 1.0},
        {1.0, 2.0, 1.0, 2.0, 1.0, 2.0},
        {0.0, 0.0, 0.0, 1e-300, 1e-300, 1e-300},
        {-1e300, 1e300, -1e300, 1e300, -1e300, 1e300},
        {1e-300, 2e-300, 3e-300, 4e-300, 5e-300, 6e-300},
    };
    for (const auto& v : controls) {
        const auto [left, right] = production(v);
        const double low = *std::min_element(std::begin(v), std::end(v));
        const double high = *std::max_element(std::begin(v), std::end(v));
        require(left >= low && left <= high, "left face left the stencil range");
        require(right >= low && right <= high, "right face left the stencil range");
        check_matches_reference(v, "control stencil reference");
    }
}

// Deterministic stencil sweep: every six-entry combination of small integer
// levels, then the same levels offset by an affine ramp and by a fixed
// fractional step. The reference comparison is what pins the shipped method to
// the frozen coefficient C = 1.25 and to the frozen extremum/CW branch choice,
// neither of which the analytic checks alone can separate.
void reference_sweep()
{
    for (int code = 0; code < 15625; ++code) {
        double v[6];
        int digits = code;
        for (int k = 0; k < 6; ++k) {
            v[k] = static_cast<double>((digits % 5) - 2);
            digits /= 5;
        }
        check_matches_reference(v, "integer stencil sweep");
    }
    for (int code = 0; code < 15625; ++code) {
        double v[6];
        int digits = code;
        for (int k = 0; k < 6; ++k) {
            v[k] = 0.375 * static_cast<double>((digits % 5) - 2)
                 + 0.125 * static_cast<double>(k) - 0.0625;
            digits /= 5;
        }
        check_matches_reference(v, "ramped stencil sweep");
        if (code % 977 == 0) {
            for (double factor : {1e-100, 1e100}) {
                double scaled[6];
                for (int k = 0; k < 6; ++k) scaled[k] = v[k] * factor;
                check_matches_reference(scaled, "scaled stencil sweep");
            }
        }
    }
}

// Manager-reproduced positive witnesses plus their sign-reversed controls and
// scales. Every stencil entry is perturbed by one and two ULP steps in both
// directions; the continuous projection/blend must keep each face within
// 64 eps of the stencil scale for every perturbation, and the reference must
// still reproduce the perturbed value.
void perturbation_witness(const double* v, const char* label)
{
    const auto [base_left, base_right] = production(v);
    check_matches_reference(v, label);
    const double band = kCurvatureBand * kEps * stencil_scale(v);

    for (int entry = 0; entry < 6; ++entry) {
        for (double target : {std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity()}) {
            double perturbed[6];
            for (int k = 0; k < 6; ++k) perturbed[k] = v[k];
            for (int step = 0; step < 2; ++step) {
                perturbed[entry] = std::nextafter(perturbed[entry], target);
                const auto [left, right] = production(perturbed);
                require(std::abs(left - base_left) <= band,
                    "ULP-perturbed witness left face jump exceeds 64 eps");
                require(std::abs(right - base_right) <= band,
                    "ULP-perturbed witness right face jump exceeds 64 eps");
                check_matches_reference(perturbed, "ULP-perturbed witness reference");
            }
        }
    }
}

void perturbation_witnesses()
{
    const double witnesses[][6] = {
        {0.7, 0.5, 1.0, 1.2, 0.5, 0.9},
        {0.25, 0.25, 0.25, 4.0, 4.0, 0.25},
    };
    for (const auto& v : witnesses) {
        perturbation_witness(v, "positive witness");

        double reversed[6];
        for (int k = 0; k < 6; ++k) reversed[k] = -v[k];
        perturbation_witness(reversed, "sign-reversed witness");

        for (double factor : {1e-100, 1e100}) {
            double scaled[6];
            for (int k = 0; k < 6; ++k) scaled[k] = v[k] * factor;
            perturbation_witness(scaled, "scaled witness");
        }
    }
}

// Bounded deterministic perturbation sweep over small integer stencils: one ULP
// in each direction for every entry must stay inside the same 64 eps band. The
// analytic and reference checks above pin the formulas; this sweep exercises the
// continuous branch joins on a dense deterministic family.
void perturbation_sweep()
{
    for (int code = 0; code < 1296; ++code) { // 6^4 small stencils
        double v[6];
        int digits = code;
        for (int k = 0; k < 6; ++k) {
            v[k] = static_cast<double>((digits % 6) - 1);
            digits /= 6;
        }
        const auto [base_left, base_right] = production(v);
        const double band = kCurvatureBand * kEps * stencil_scale(v);
        for (int entry = 0; entry < 6; ++entry) {
            for (double target : {1e300, -1e300}) {
                double perturbed[6];
                for (int k = 0; k < 6; ++k) perturbed[k] = v[k];
                perturbed[entry] = std::nextafter(perturbed[entry], target);
                const auto [left, right] = production(perturbed);
                require(std::abs(left - base_left) <= band,
                    "sweep-perturbed left face jump exceeds 64 eps");
                require(std::abs(right - base_right) <= band,
                    "sweep-perturbed right face jump exceeds 64 eps");
            }
        }
    }
}

// Native conservative scalar references below integrate actual polynomial
// fields analytically. Production moments are inputs to the implementation,
// never the oracle for means/centroids/profile integrals in these tests.
namespace native_scalar_checks {
namespace w = RzReconstruction::weighted;

long double power(long double x, int n)
{
    long double value=1.;for(int k=0;k<n;++k)value*=x;return value;
}
long double integral_power(long double a,long double b,int n)
{
    return (power(b,n+1)-power(a,n+1))/(n+1);
}
// Whole-negative V has |r|=-r; that common minus cancels in this ratio.
long double analytic_mean(const std::array<long double,4>& q,double a,double b,bool angular)
{
    const int p=angular?2:1;long double value=0.;
    for(int n=0;n<4;++n)value+=q[n]*integral_power(a,b,n+p);
    return value/integral_power(a,b,p);
}
long double analytic_value(const std::array<long double,4>& q,long double r)
{
    return q[0]+r*(q[1]+r*(q[2]+r*q[3]));
}
void close(double value,long double reference,double scale,const char* message)
{
    const long double tolerance=64.L*kEps*std::max(static_cast<long double>(scale),std::abs(reference));
    require(std::isfinite(value)&&std::isfinite(reference)
        &&std::abs(static_cast<long double>(value)-reference)<=tolerance,message);
}
template<std::size_t N>
std::array<w::Cell,N> cells(double lower,bool angular)
{
    std::array<w::Cell,N> result;
    for(std::size_t n=0;n<N;++n)
        require(w::bind_cell(lower+double(n),lower+double(n)+1.,angular,result[n]),
            "actual one-sided scalar cell rejected");
    return result;
}
template<std::size_t N>
std::array<double,N> means(const std::array<long double,4>& q,const std::array<w::Cell,N>& c)
{
    std::array<double,N> result;
    for(std::size_t n=0;n<N;++n)result[n]=double(analytic_mean(q,c[n].lower,c[n].upper,c[n].angular));
    return result;
}
template<std::size_t N> double scale_of(const std::array<double,N>& q)
{
    double scale=0.;for(double v:q)scale=std::max(scale,std::abs(v));return scale;
}
// Expand the ACTUAL returned own-xi coefficients into a physical polynomial,
// then integrate with independent long-double antiderivatives under V/W.
void conserved(const w::Profile& p,double mean,bool angular,double scale)
{
    require(p.valid,"selected scalar profile invalid");
    const long double a=p.lower,h=static_cast<long double>(p.upper)-a;
    const long double c0=p.polynomial.constant,c1=p.polynomial.linear,c2=p.polynomial.quadratic;
    const std::array<long double,4> physical{c0-c1*a/h+c2*a*a/(h*h),c1/h-2*c2*a/(h*h),c2/(h*h),0.};
    close(double(analytic_mean(physical,p.lower,p.upper,angular)),mean,scale,
        "full selected donor lost its own V/W mean");
}
bool same(const w::Profile& a,const w::Profile& b)
{
    const double x[]{a.polynomial.constant,a.polynomial.linear,a.polynomial.quadratic,a.lower,a.upper,a.mean};
    const double y[]{b.polynomial.constant,b.polynomial.linear,b.polynomial.quadratic,b.lower,b.upper,b.mean};
    for(int n=0;n<6;++n)if(std::bit_cast<std::uint64_t>(x[n])!=std::bit_cast<std::uint64_t>(y[n]))return false;
    return a.valid==b.valid;
}

void muscl_identity()
{
    for(bool angular:{false,true})for(double origin:{0.,2.,-4.}) {
        const auto c=cells<4>(origin,angular);
        const std::array<long double,4> q{2.L,.5L,0.,0.};const auto v=means(q,c);
        w::Profile a,b,sa,sb;
        require(w::muscl<MinMod>(v,c,a,b)&&w::muscl<SuperBee>(v,c,sa,sb),"affine selected MUSCL rejected");
        const double scale=scale_of(v);
        for(int donor=0;donor<2;++donor) {
            const auto& m=donor?b:a;const auto& s=donor?sb:sa;const auto& cell=c[donor+1];
            conserved(m,v[donor+1],angular,scale);conserved(s,v[donor+1],angular,scale);
            for(double r:{cell.lower,cell.upper}) {
                const auto exact=analytic_value(q,r);
                close(m.at(r),exact,scale,"MinMod affine weighted endpoint");
                close(s.at(r),exact,scale,"SuperBee affine weighted endpoint");
            }
        }
        // Nonlinear means give unequal one-sided gradients. Actual MinMod and
        // SuperBee must differ; affine data instead MUST agree (ratio=1).
        const std::array<double,4> shaped{0.,1.,3.,6.};
        require(w::muscl<MinMod>(shaped,c,a,b)&&w::muscl<SuperBee>(shaped,c,sa,sb),"nonlinear limiter stencil rejected");
        require(a.at(c[1].upper)!=sa.at(c[1].upper),"selected MUSCL limiter was bypassed");
        const double centroid_low=double(analytic_mean({0.,1.,0.,0.},c[0].lower,c[0].upper,angular));
        const double centroid=double(analytic_mean({0.,1.,0.,0.},c[1].lower,c[1].upper,angular));
        const double centroid_high=double(analytic_mean({0.,1.,0.,0.},c[2].lower,c[2].upper,angular));
        const double backward=1./(centroid-centroid_low),forward=2./(centroid_high-centroid);
        const double ratio=backward/forward;
        const double minmod=std::min(1.,ratio),superbee=std::max(std::min(2.*ratio,1.),std::min(ratio,2.));
        close(a.at(c[1].upper),1.+minmod*forward*(c[1].upper-centroid),6.,"actual MinMod analytic gradient");
        close(sa.at(c[1].upper),1.+superbee*forward*(c[1].upper-centroid),6.,"actual SuperBee analytic gradient");
        conserved(a,1.,angular,6.);conserved(b,3.,angular,6.);
        conserved(sa,1.,angular,6.);conserved(sb,3.,angular,6.);
    }
}

void ppm_polynomials()
{
    for(bool angular:{false,true})for(double origin:{0.,2.,-6.}) {
        const auto c=cells<6>(origin,angular);
        const long double center=origin+2.5L;
        // A true smooth minimum inside the left donor: supported curvature
        // may keep it, so this test never requires every final curve monotone.
        const std::array<long double,4> quadratic{1.L+center*center,-2.L*center,1.L,0.};
        const auto v=means(quadratic,c);w::Profile left,right;
        require(w::ppm(v,c,left,right),"weighted smooth quadratic PPM rejected");
        for(int n=0;n<2;++n) {
            const auto& p=n?right:left;const auto& cell=c[n+2];
            conserved(p,v[n+2],angular,scale_of(v));
            for(double r:{cell.lower,cell.upper})close(p.at(r),analytic_value(quadratic,r),scale_of(v),
                "weighted quadratic full donor endpoint");
        }
        // Cubic exactness belongs to the four-mean FACE interpolation, not
        // the full cell parabola (which cannot be an arbitrary cubic).
        const std::array<long double,4> cubic{2.L,3.L,.5L,1.L/64.L};const auto cv=means(cubic,c);
        for(int n=0;n<3;++n) {
            double face=0.;const double r=c[n+1].upper;
            require(w::cubic_face(cv.data()+n,c.data()+n,r,c[n+1].upper-c[n+1].lower,face),"true cubic face solve rejected");
            close(face,analytic_value(cubic,r),scale_of(cv),"four weighted cubic means lost exact face");
        }
        // Arbitrary finite six-point means exercise conservation independently
        // of polynomial interpolation or a supported-extremum assertion.
        const std::array<double,6> isolated{.7,.5,1.,1.2,.5,.9};
        require(w::ppm(isolated,c,left,right),"finite isolated weighted PPM rejected");
        conserved(left,isolated[2],angular,scale_of(isolated));conserved(right,isolated[3],angular,scale_of(isolated));
        const std::array<double,6> zero{};
        require(w::ppm(zero,c,left,right),"weighted exact-zero PPM rejected");
        conserved(left,0.,angular,0.);conserved(right,0.,angular,0.);
        require(left.at(c[2].lower)==0.&&left.at(c[2].upper)==0.
            &&right.at(c[3].lower)==0.&&right.at(c[3].upper)==0.,
            "weighted exact-zero endpoints drifted");
    }
}

void cw_geometry()
{
    for(bool angular:{false,true})for(bool reflected:{false,true}) {
        w::Cell cell;require(w::bind_cell(reflected?-1.:0.,reflected?0.:1.,angular,cell),"axis CW cell rejected");
        const double factor_left=reflected?(angular?2./3.:1.):(angular?9.:5.);
        const double factor_right=reflected?(angular?9.:5.):(angular?2./3.:1.);
        for(double sign:{1.,-1.})for(int side:{0,1}) {
            double l=sign*(side==0?-2.*factor_left:-1.),r=sign*(side==1?2.*factor_right:1.);
            require(w::cw_bound(l,r,0.,cell.xi),"weighted CW rejected valid geometry");
            close(l,sign*(side==0?-factor_left:-1.),2.*std::max(factor_left,factor_right),"weighted CW left analytic factor");
            close(r,sign*(side==1?factor_right:1.),2.*std::max(factor_left,factor_right),"weighted CW right analytic factor");
            // Use independent p-weighted xi moments from antiderivatives.
            const long double mu1=analytic_mean({-cell.lower,1.,0.,0.},cell.lower,cell.upper,angular);
            const std::array<long double,4> xi2{cell.lower*cell.lower,-2.L*cell.lower,1.,0.};
            const long double mu2=analytic_mean(xi2,cell.lower,cell.upper,angular),delta=r-l;
            const long double a=(-l-delta*mu1)/(mu1-mu2);
            const long double band=64.L*kEps*std::max(std::abs(delta),std::abs(a));
            require(sign*(delta+a)>=-band&&sign*(delta-a)>=-band,"weighted CW derivative sign");
        }
    }
}

void own_cell_ordinary_bits()
{
    const double controls[][6]={{1,1,1,1,1,1},{1,1.5,2,2.5,3,3.5},{.7,.5,1,1.2,.5,.9},
        {1,1,1,2,2,2},{.25,.25,.25,4,4,.25},{0,0,0,0,0,0}};
    for(const auto& v:controls) {
        double six[6],low[5],high[5];for(int n=0;n<6;++n)six[n]=v[n];
        for(int n=0;n<5;++n){low[n]=v[n];high[n]=v[n+1];}
        double a=0.,b=0.,ll=0.,lr=0.,rl=0.,rr=0.;
        PPMReconstruction::reconstruct_scalar_ppm(six,a,b);
        PPMReconstruction::reconstruct_scalar_cell_ppm(low,ll,lr);
        PPMReconstruction::reconstruct_scalar_cell_ppm(high,rl,rr);
        require(std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(lr)
            &&std::bit_cast<std::uint64_t>(b)==std::bit_cast<std::uint64_t>(rl),"ordinary owning-cell extraction changed interface bits");
        double expected_ll=reference_face(reference_interpolate_face(v[0],v[1],v[2],v[3]),v[0],v[1],v[2],v[3]);
        double expected_lr=reference_face(reference_interpolate_face(v[1],v[2],v[3],v[4]),v[1],v[2],v[3],v[4]);
        double expected_rl=expected_lr;
        double expected_rr=reference_face(reference_interpolate_face(v[2],v[3],v[4],v[5]),v[2],v[3],v[4],v[5]);
        reference_cell(expected_ll,expected_lr,v[0],v[1],v[2],v[3],v[4]);
        reference_cell(expected_rl,expected_rr,v[1],v[2],v[3],v[4],v[5]);
        const double scale=stencil_scale(v);
        close_relative(ll,expected_ll,scale,"ordinary lower endpoint old oracle");
        close_relative(lr,expected_lr,scale,"ordinary upper endpoint old oracle");
        close_relative(rl,expected_rl,scale,"ordinary next lower old oracle");
        close_relative(rr,expected_rr,scale,"ordinary next upper old oracle");
    }
}

void rejection_atomicity()
{
    auto c=cells<6>(0.,false);const std::array<double,6> original{1,2,3,4,5,6};
    const w::Profile sentinel{{42.,3.,4.},17.,18.,23.,true};
    for(int fault=0;fault<4;++fault) {
        auto geometry=c;auto v=original;w::Profile left=sentinel,right=sentinel;
        if(fault==0)v[5]=std::numeric_limits<double>::quiet_NaN();
        if(fault==1)geometry[2].xi.first=0.;
        if(fault==2)geometry[2].lower=-.25; // Actual support crosses the axis.
        if(fault==3)geometry[3].lower=geometry[3].upper; // Zero physical spacing.
        require(!w::ppm(v,geometry,left,right),"illegal weighted PPM input accepted");
        require(same(left,sentinel)&&same(right,sentinel),"rejected weighted PPM published partial donor");
    }
    w::Cell cell;
    require(!w::bind_cell(-.25,.25,false,cell),"cross-axis selected cell accepted");
    require(!w::bind_cell(0.,std::numeric_limits<double>::infinity(),false,cell),"nonfinite selected support accepted");
    double singular[3][4]={{1,0,0,1},{2,0,0,2},{3,0,0,3}};
    double a=17.,b=18.,d=19.;
    require(!RzReconstruction::solve_cubic_differences(singular,a,b,d),"singular actual difference matrix accepted");
    require(a==17.&&b==18.&&d==19.,"singular difference solve published coefficients");
    auto m=cells<4>(0.,true);const std::array<double,4> bad{1.,2.,3.,std::numeric_limits<double>::infinity()};
    w::Profile left=sentinel,right=sentinel;
    require(!w::muscl<MinMod>(bad,m,left,right)&&same(left,sentinel)&&same(right,sentinel),"nonfinite MUSCL published partial profiles");
}
void run()
{
    muscl_identity();ppm_polynomials();cw_geometry();own_cell_ordinary_bits();rejection_atomicity();
    std::cout<<"Native selected scalar mathematics checks passed (independent V/W integrals, 64 eps; no full method grant)\n";
}
} // namespace native_scalar_checks

/**
 * Independent scalar parity-leaf witnesses.
 *
 * Workflow: form exact polynomial cell means by long-double antiderivatives,
 * call the selected production limiter, and independently integrate/evaluate
 * its public profile. These checks grant neither a full EOS nor AMR method.
 * Radius normalization keeps the large-radius/small-amplitude witness real;
 * the oracle never divides a double-precision angular mean by a huge radius.
 */
namespace native_parity_checks {
namespace p = RzReconstruction::parity;

long double power(long double value, int exponent)
{
    long double result = 1.L;
    for (int n = 0; n < exponent; ++n) result *= value;
    return result;
}

long double primitive_difference(long double lower, long double upper, int exponent)
{
    return (power(upper, exponent + 1) - power(lower, exponent + 1)) /
           static_cast<long double>(exponent + 1);
}

int weight_power(p::Measure measure)
{
    return measure == p::Measure::OddW ? 2 : 1;
}

bool odd(p::Measure measure)
{
    return measure != p::Measure::EvenV;
}

/** Integrate q=A[a+b(r/R)^2] or q=A(r/R)[a+b(r/R)^2]. */
long double polynomial_mean(const p::Cell& cell, p::Measure measure,
                            long double a, long double b,
                            long double radius_scale = 1.L,
                            long double amplitude = 1.L)
{
    const long double lower = static_cast<long double>(cell.lower) / radius_scale;
    const long double upper = static_cast<long double>(cell.upper) / radius_scale;
    const int exponent = weight_power(measure);
    const int parity_power = odd(measure) ? 1 : 0;
    // The common minus sign of |r| on a reflected cell cancels in this ratio.
    return amplitude *
           (a * primitive_difference(lower, upper, exponent + parity_power) +
            b * primitive_difference(lower, upper, exponent + parity_power + 2)) /
           primitive_difference(lower, upper, exponent);
}

long double polynomial_value(long double radius, p::Measure measure,
                             long double a, long double b,
                             long double radius_scale = 1.L,
                             long double amplitude = 1.L)
{
    const long double t = radius / radius_scale;
    return amplitude * (odd(measure) ? t : 1.L) * (a + b * t * t);
}

void close(double actual, long double expected, const char* message)
{
    // Keep the existing native scalar 64-epsilon relative budget, without a
    // max(1,scale) that would hide a nonzero subnormal-scale profile.
    const long double scale = std::max(std::fabs(static_cast<long double>(actual)),
                                       std::fabs(expected));
    require(std::isfinite(actual) && std::isfinite(expected) &&
            std::fabs(static_cast<long double>(actual) - expected) <=
                64.L * static_cast<long double>(kEps) * scale, message);
}

std::array<p::Cell, 3> support(bool nonuniform, bool reflected,
                             double radius_scale = 1.)
{
    const std::array<double, 4> faces = nonuniform
        ? std::array<double, 4>{0., .375, 1.25, 3.}
        : std::array<double, 4>{0., 1., 2., 3.};
    std::array<p::Cell, 3> result{};
    for (int n = 0; n < 3; ++n) {
        const double lower = faces[n] * radius_scale;
        const double upper = faces[n + 1] * radius_scale;
        result[n] = reflected ? p::Cell{-upper, -lower} : p::Cell{lower, upper};
    }
    return result;
}

/** Exact Gauss-4 integration of this quadratic/cubic public scalar profile. */
long double profile_mean(const p::Profile& profile, const p::Cell& cell,
                         p::Measure measure)
{
    constexpr std::array<long double, 4> nodes{
        -.861136311594052575223946488893L, -.339981043584856264802665759103L,
         .339981043584856264802665759103L,  .861136311594052575223946488893L};
    constexpr std::array<long double, 4> weights{
        .347854845137453857373063949222L, .652145154862546142626936050778L,
        .652145154862546142626936050778L, .347854845137453857373063949222L};
    const long double lower = cell.lower, upper = cell.upper;
    const long double radial_scale = std::max(std::fabs(lower), std::fabs(upper));
    long double numerator = 0.L, denominator = 0.L;
    for (int n = 0; n < 4; ++n) {
        const long double radius = .5L * ((1.L - nodes[n]) * lower +
                                        (1.L + nodes[n]) * upper);
        const long double t = radius / radial_scale;
        const long double measure_weight = weight_power(measure) == 1
            ? std::fabs(t) : t * t;
        const double value = profile.at(static_cast<double>(radius));
        require(std::isfinite(value), "parity public profile is nonfinite at Gauss node");
        numerator += weights[n] * measure_weight * static_cast<long double>(value);
        denominator += weights[n] * measure_weight;
    }
    return numerator / denominator;
}

/** True eta-affine fields must retain both signed traces and their own means. */
template<class Limiter>
void polynomial_cases()
{
    constexpr std::array<p::Measure, 3> measures{
        p::Measure::EvenV, p::Measure::OddV, p::Measure::OddW};
    for (bool nonuniform : {false, true}) for (bool reflected : {false, true}) {
        const auto geometry = support(nonuniform, reflected);
        for (const auto measure : measures) {
            std::array<double, 3> means{};
            for (int n = 0; n < 3; ++n)
                means[n] = static_cast<double>(polynomial_mean(geometry[n], measure, 1.25L, .125L));
            for (int target : {0, 1}) {
                p::Profile profile{};
                require(p::reconstruct<Limiter>(means, geometry, target, measure, profile) && profile.valid,
                        "legal signed parity polynomial was rejected");
                const auto& cell = geometry[target];
                close(static_cast<double>(profile_mean(profile, cell, measure)), means[target],
                      "parity profile did not conserve its own V/W native mean");
                for (long double fraction : {0.L, .069431844202973712388026755554L,
                                              .330009478207571867598667120449L,
                                              .669990521792428132401332879551L,
                                              .930568155797026287611973244446L, 1.L}) {
                    const long double radius = (1.L - fraction) * cell.lower + fraction * cell.upper;
                    const double value = profile.at(static_cast<double>(radius));
                    close(value, polynomial_value(radius, measure, 1.25L, .125L),
                          "parity eta-affine trace/Gauss value differs from independent polynomial");
                }
                if (odd(measure) && target == 0)
                    require(profile.at(0.) == 0., "odd parity profile is nonzero at the axis");
            }
        }
    }
}

/** Independent physical centroids generate a nonlinear limiter-identity case. */
void selected_limiter_identity()
{
    for (bool reflected : {false, true}) {
        const auto geometry = support(true, reflected);
        for (const auto measure : {p::Measure::EvenV, p::Measure::OddV, p::Measure::OddW}) {
            std::array<long double, 3> centroids{};
            for (int n = 0; n < 3; ++n) {
                const int exponent = weight_power(measure) + (odd(measure) ? 1 : 0);
                centroids[n] = primitive_difference(geometry[n].lower, geometry[n].upper, exponent + 2) /
                               primitive_difference(geometry[n].lower, geometry[n].upper, exponent);
            }
            // Piecewise constant a (or q=r*a) has independently exact means.
            // The two positive gradients have ratio 1/2 in their true centroid
            // coordinates; this is deliberately separate from affine exactness.
            const std::array<long double, 3> amplitudes{
                1.L, 2.L, 2.L + 2.L * (centroids[2] - centroids[1]) /
                                            (centroids[1] - centroids[0])};
            std::array<double, 3> means{};
            for (int n = 0; n < 3; ++n)
                means[n] = static_cast<double>(polynomial_mean(geometry[n], measure, amplitudes[n], 0.L));
            p::Profile minmod{}, mc{}, superbee{};
            require(p::reconstruct<MinMod>(means, geometry, 1, measure, minmod) &&
                    p::reconstruct<McLimiter>(means, geometry, 1, measure, mc) &&
                    p::reconstruct<SuperBee>(means, geometry, 1, measure, superbee) &&
                    minmod.valid && mc.valid && superbee.valid,
                    "selected nonlinear parity witness was rejected");
            for (const p::Profile* profile : {&minmod, &mc, &superbee})
                close(static_cast<double>(profile_mean(*profile, geometry[1], measure)), means[1],
                      "selected limiter changed the native parity cell mean");
            const double outer_radius = reflected ? geometry[1].lower : geometry[1].upper;
            const double sign = odd(measure) && reflected ? -1. : 1.;
            const double mm = sign * minmod.at(outer_radius);
            const double central = sign * mc.at(outer_radius);
            const double sb = sign * superbee.at(outer_radius);
            require(std::isfinite(mm) && std::isfinite(central) && std::isfinite(sb) &&
                    mm < central && central < sb,
                    "native nonlinear reconstruction lost actual MinMod/MC/SuperBee distinction");
        }
    }
}

/** A representable tiny mean must survive a huge radius without j/r loss. */
void scaled_nonzero_amplitude()
{
    const long double field_scale = 3.e100L, amplitude = 1.e-250L;
    for (bool reflected : {false, true}) {
    const auto geometry = support(false, reflected, 1.e100);
    for (const auto measure : {p::Measure::OddV, p::Measure::OddW}) {
        std::array<double, 3> means{};
        for (int n = 0; n < 3; ++n) {
            means[n] = static_cast<double>(polynomial_mean(geometry[n], measure, 1.L, .25L,
                                                          field_scale, amplitude));
            require(std::isfinite(means[n]) && (reflected ? -means[n] : means[n]) > 0.,
                    "independent tiny parity mean is not representable");
            require(means[n] / 3.e100 == 0.,
                    "large-radius witness does not actually expose double j/r underflow");
        }
        for (int target : {0, 1}) {
            p::Profile profile{};
            require(p::reconstruct<McLimiter>(means, geometry, target, measure, profile) && profile.valid,
                    "large-radius representable parity profile was rejected");
            const double radius = reflected ? geometry[target].lower : geometry[target].upper;
            require((reflected ? -profile.at(radius) : profile.at(radius)) > 0.,
                    "parity leaf silently zeroed representable tiny amplitude");
            close(profile.at(radius), polynomial_value(radius, measure, 1.L, .25L,
                                                        field_scale, amplitude),
                  "scaled tiny parity trace differs from independent polynomial");
            close(static_cast<double>(profile_mean(profile, geometry[target], measure)), means[target],
                  "scaled tiny parity native mean was not preserved");
            if (target == 0)
                require(profile.at(0.) == 0., "scaled first-donor odd profile violates axis regularity");
            else
                require(!std::isfinite(profile.at(0.)),
                        "scaled centered parity profile evaluated outside its actual donor");
        }
    }
    }
}

bool same_observable_profile(const p::Profile& first, const p::Profile& second)
{
    if (first.valid != second.valid) return false;
    for (double radius : {1., 1.125, 1.5, 2.})
        if (std::bit_cast<std::uint64_t>(first.at(radius)) !=
            std::bit_cast<std::uint64_t>(second.at(radius))) return false;
    return true;
}

/** Illegal support must fail without publishing even an observable profile. */
void rejection_atomicity()
{
    const auto original_geometry = support(false, false);
    const std::array<double, 3> original_means{1., 2., 3.};
    p::Profile sentinel{};
    require(p::reconstruct<McLimiter>(original_means, original_geometry, 1,
                                     p::Measure::EvenV, sentinel) && sentinel.valid,
            "parity rejection sentinel could not be formed");
    for (int fault = 0; fault < 14; ++fault) {
        auto geometry = original_geometry;
        auto means = original_means;
        int target = 1;
        auto measure = p::Measure::EvenV;
        switch (fault) {
        case 0: means[1] = std::numeric_limits<double>::quiet_NaN(); break;
        case 1: means[2] = std::numeric_limits<double>::infinity(); break;
        case 2: geometry[1].lower = std::numeric_limits<double>::infinity(); break;
        case 3: geometry[2].upper = geometry[2].lower; break; // Only two real cells.
        case 4: geometry[1].lower += .125; break; // Actual gap.
        case 5: geometry[1].lower -= .125; break; // Actual overlap.
        case 6: geometry[0].lower = -.125; break; // Axis-straddling cell.
        case 7: geometry[2] = {-3., -2.}; break; // Mixed signs.
        case 8: geometry[1] = {2., 1.}; break; // Reversed physical endpoints.
        case 9: std::swap(geometry[0], geometry[2]); break; // Wrong |r| order.
        case 10: target = 2; break;
        case 11: target = -1; break;
        case 12: measure = static_cast<p::Measure>(255); break;
        case 13: geometry[0] = {0., 0.}; break;
        }
        p::Profile output = sentinel;
        require(!p::reconstruct<McLimiter>(means, geometry, target, measure, output),
                "illegal parity support/measure/target was accepted");
        require(same_observable_profile(output, sentinel),
                "rejected parity leaf published a partial observable profile");
    }
}

void run()
{
    polynomial_cases<MinMod>();
    polynomial_cases<McLimiter>();
    polynomial_cases<SuperBee>();
    selected_limiter_identity();
    scaled_nonzero_amplitude();
    rejection_atomicity();
    std::cout << "Native scalar parity independent polynomial checks passed (64 eps; no full method grant)\n";
}
} // namespace native_parity_checks

} // namespace

int main()
{
    try {
        constants();
        affine_fields();
        exact_quadratics();
        witness_continuity();
        discontinuity_controls();
        reference_sweep();
        perturbation_witnesses();
        perturbation_sweep();
        native_scalar_checks::run();
        native_parity_checks::run();
        std::cout << "PPM curvature limiter checks passed (C=1.25, 64 eps witness band)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
