/**
 * @file RzMetricCases.h
 * @brief Independent full-rotation finite-volume references for shared math.
 *
 * Stored measure values integrate the full source measure with 70/100-digit Decimal,
 * then round once to binary64. Endpoints are the exact binary64 input values.
 * Reproduce with validation/amr/rz_metric_reference.py; no production formulas
 * are called by that oracle. Gauss sample moments separately use long-double
 * polynomial antiderivatives. Neither check qualifies hydro/gravity/AMR evolution.
 */
#pragma once
#include "grid/GridMetrics.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace RzMetricCases {
/** Independently integrate r^power under r^measure dr in long double. */
inline long double mean_power(long double lo,long double hi,int power,int measure)
{
    return (measure+1.L)/(power+measure+1.L)
        *(std::pow(hi,power+measure+1)-std::pow(lo,power+measure+1))
        /(std::pow(hi,measure+1)-std::pow(lo,measure+1));
}

struct MeasureCase {
    double lower, upper, dz;
    double volume, lower_radial_area, upper_radial_area, axial_area;
};
inline constexpr MeasureCase cases[]{
    // BEGIN INDEPENDENT RZ MEASURE DATA
    {0x0.0p+0, 0x1.0000000000000p-2, 0x1.0000000000000p-3, 0x1.921fb54442d18p-6, 0x0.0p+0, 0x1.921fb54442d18p-3, 0x1.921fb54442d18p-3},
    {0x0.0p+0, 0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.921fb54442d18p+1, 0x0.0p+0, 0x1.921fb54442d18p+2, 0x1.921fb54442d18p+1},
    {0x1.0000000000000p+0, 0x1.0000000000000p+1, 0x1.0000000000000p-1, 0x1.2d97c7f3321d2p+2, 0x1.921fb54442d18p+1, 0x1.921fb54442d18p+2, 0x1.2d97c7f3321d2p+3},
    {0x1.e848000000000p+19, 0x1.e848200000000p+19, 0x1.0624dd2f1a9fcp-10, 0x1.88b303e2dc1bep+12, 0x1.88b2f704a940ap+12, 0x1.88b310c10ef73p+12, 0x1.7f7ed1cb8af34p+22},
    {0x1.2a05f20000000p+33, 0x1.2a05f20080000p+33, 0x1.0000000000000p+0, 0x1.d4223fc25dffap+35, 0x1.d4223fc1f977cp+35, 0x1.d4223fc2c2879p+35, 0x1.d4223fc25dffap+35},
    {0x1.b7cdfd9d7bdbbp-34, 0x1.b7cf1dd875ca2p-34, 0x1.5798ee2308c3ap-27, 0x1.04fe9ede0437bp-107, 0x1.cf9e03ca01d95p-58, 0x1.cf9f33a012e9ep-58, 0x1.84e98b0a8c106p-81},
    {0x1.0000000000000p+0, 0x1.000000006df38p+0, 0x1.0000000000000p+0, 0x1.596bfaae3fef5p-31, 0x1.921fb54442d18p+2, 0x1.921fb544ef878p+2, 0x1.596bfaae3fef5p-31},
    {0x1.38d352e5096afp+498, 0x1.38d352e58fc67p+498, 0x1.bff2ee48e0530p-333, 0x1.c344001dcc759p+633, 0x1.ade9b959b7cd0p+168, 0x1.ade9b95a70726p+168, 0x1.01e53ceaa01d1p+966},
    {0x1.bff2ee48e0530p-333, 0x1.bff2ee48e0530p-332, 0x1.bff2ee48e0530p-333, 0x1.93f3009f121a0p-994, 0x1.33ce50895f104p-662, 0x1.33ce50895f104p-661, 0x1.cdb578ce0e987p-662},
    {0x1.a2fe76a3f9475p-499, 0x1.a2fe76a3f9475p-498, 0x1.249ad2594c37dp+332, 0x1.cdb578ce0e987p-662, 0x1.782188df45c21p-164, 0x1.782188df45c21p-163, 0x1.93f3009f1219fp-994},
    // END INDEPENDENT RZ MEASURE DATA
};

ARCH_HOST_DEVICE inline double relative_error(const MeasureCase& c)
{
    const double actual[]{
        GridMetrics::Rz::CellVolume(c.lower,c.upper,c.dz),
        GridMetrics::Rz::RadialFaceArea(c.lower,c.dz),
        GridMetrics::Rz::RadialFaceArea(c.upper,c.dz),
        GridMetrics::Rz::AxialFaceArea(c.lower,c.upper)};
    const double expected[]{c.volume,c.lower_radial_area,c.upper_radial_area,c.axial_area};
    double error=0.;
    for (unsigned i=0;i<4;++i) {
        if (!std::isfinite(actual[i])) return std::numeric_limits<double>::infinity();
        if (expected[i]==0.) {
            if (actual[i]!=0. || std::signbit(actual[i]))
                return std::numeric_limits<double>::infinity();
        } else {
            if (actual[i]<=0.) return std::numeric_limits<double>::infinity();
            error=std::max(error,std::abs((actual[i]-expected[i])/expected[i]));
        }
    }
    return error;
}
inline double conditioning_error()
{
    double error=0.;
    for (const auto& c:cases) error=std::max(error,relative_error(c));
    return error;
}

/** Compare independent long-double sample moments with the original window. */
inline void check_sample_moment(long double actual,long double expected,const char* label)
{
    if(!std::isfinite(actual)||std::abs(actual-expected)>2.e-12L*std::max(1.L,std::abs(expected)))
        throw std::runtime_error(label);
}

/** Qualify the shared Gauss-4 radial times Gauss-2 axial sampling rule.
 * Expected monomial means use source antiderivatives in mean_power(), never
 * shared quadrature nodes/weights. V includes r dr and W includes r^2 dr;
 * consequently radial degrees 6/5 and axial degree 3 are exact mathematically.
 * The numerical check retains finite-precision tolerance and is not an
 * initialization/hierarchy scientific certificate.
 */
inline void cell_average_samples()
{
    constexpr auto existing=GridMetrics::GeometrySemantics::Existing;
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    struct SampleRange {double radial_lo,radial_hi,axial_lo,axial_hi;};
    constexpr SampleRange ranges[]{
        {0.,1.,-.75,1.25}, {0.,.125,2.,2.5},
        {1.,2.,-2.,-1.25}, {3.,3.25,0.,.375}};
    for(const auto& range:ranges) {
        const double dr=range.radial_hi-range.radial_lo,dz=range.axial_hi-range.axial_lo;
        Grid grid(amr::MAX_NG,range.radial_lo,range.radial_lo+amr::BLOCK_NX*dr,
            range.axial_lo,range.axial_lo+amr::BLOCK_NY*dz,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
        const auto native=GridMetrics::make_geometry_view(grid,rz);
        const auto original=GridMetrics::make_geometry_view(grid);
        if(native.semantics!=rz||native.geometry!=GridMetrics::Geometry::Cylindrical
           ||native.dim!=2||original.semantics!=rz
           ||native.ng!=original.ng||native.stride_y!=original.stride_y
           ||native.stride_z!=original.stride_z||native.total_size!=original.total_size
           ||native.dx1!=original.dx1||native.dx2!=original.dx2||native.dx3!=original.dx3
           ||native.x1_min!=original.x1_min||native.x2_min!=original.x2_min
           ||native.x3_min!=original.x3_min||native.geometry!=original.geometry
           ||native.dim!=original.dim)
            throw std::runtime_error("RZ sample factory changed the actual native layout");
        bool retired_rejected=false;
        try {(void)GridMetrics::make_geometry_view(grid,existing);}
        catch(const std::invalid_argument&) {retired_rejected=true;}
        if(!retired_rejected)
            throw std::runtime_error("RZ sample factory accepted a retired cylindrical polar chart");
        bool rejected=false;
        try {
            (void)GridMetrics::make_geometry_view(grid,
                static_cast<GridMetrics::GeometrySemantics>(255));
        } catch(const std::invalid_argument&) {rejected=true;}
        if(!rejected)throw std::runtime_error("RZ sample factory accepted unknown geometry semantics");

        const double left=grid.GetFacePosL(grid.Is()),right=grid.GetFacePosR(grid.Is());
        const double z_lower=grid.x2_min,z_upper=grid.x2_min+grid.dx2;
        if(left!=range.radial_lo||right!=range.radial_hi||z_lower!=range.axial_lo
           ||z_upper!=range.axial_hi)
            throw std::runtime_error("RZ sample fixture did not bind its actual cell bounds");
        const auto samples=GridMetrics::Rz::CellAverageSamples(left,right,z_lower,z_upper);
        if(samples.size()!=8)throw std::runtime_error("RZ sample rule lost radial-four/axial-two product");
        long double sum_v=0.,sum_w=0.;
        for(const auto& sample:samples) {
            if(!std::isfinite(sample.radius)||!std::isfinite(sample.axial)
               ||!std::isfinite(sample.volume_weight)||!std::isfinite(sample.angular_weight)
               ||!(sample.radius>left&&sample.radius<right)
               ||!(sample.axial>z_lower&&sample.axial<z_upper)
               ||!(sample.volume_weight>0.)||!(sample.angular_weight>0.))
                throw std::runtime_error("RZ sample rule published invalid nodes/weights outside actual bounds");
            sum_v+=sample.volume_weight;sum_w+=sample.angular_weight;
        }
        check_sample_moment(sum_v,1.L,"RZ sample V weights are not normalized");
        check_sample_moment(sum_w,1.L,"RZ sample W weights are not normalized");

        for(int measure=1;measure<=2;++measure) {
            const int radial_degree=measure==1?6:5;
            for(int power=0;power<=radial_degree;++power) {
                long double actual=0.;
                for(const auto& sample:samples)
                    actual+=(measure==1?sample.volume_weight:sample.angular_weight)
                        *std::pow(static_cast<long double>(sample.radius),power);
                check_sample_moment(actual,mean_power(left,right,power,measure),
                    "RZ sampled radial monomial differs from independent antiderivative");
            }
            for(int power=0;power<=3;++power) {
                long double actual=0.;
                for(const auto& sample:samples)
                    actual+=(measure==1?sample.volume_weight:sample.angular_weight)
                        *std::pow(static_cast<long double>(sample.axial),power);
                check_sample_moment(actual,mean_power(z_lower,z_upper,power,0),
                    "RZ sampled axial monomial differs from independent antiderivative");
            }
            // Separate physical measures factor exactly for these tensor-product
            // source monomials, including the highest-degree qualified product.
            const int cross_powers[][2]{{1,2},{radial_degree-1,1},{radial_degree,3}};
            for(const auto& cross:cross_powers) {
                long double actual=0.;
                for(const auto& sample:samples)
                    actual+=(measure==1?sample.volume_weight:sample.angular_weight)
                        *std::pow(static_cast<long double>(sample.radius),cross[0])
                        *std::pow(static_cast<long double>(sample.axial),cross[1]);
                check_sample_moment(actual,mean_power(left,right,cross[0],measure)
                    *mean_power(z_lower,z_upper,cross[1],0),
                    "RZ sampled mixed monomial differs from independent product integral");
            }
        }
    }
    Grid cartesian(amr::MAX_NG,0.,1.,0.,1.,0.,1.);
    cartesian.dim=2;cartesian.geometry="cartesian";cartesian.InitializeTopology();
    const auto cartesian_view=GridMetrics::make_geometry_view(cartesian,existing);
    if(cartesian_view.semantics!=existing||cartesian_view.geometry!=GridMetrics::Geometry::Cartesian
       ||cartesian_view.dim!=2||cartesian_view.total_size!=cartesian.GetTotalSize())
        throw std::runtime_error("RZ sample guard changed legal existing Cartesian factory behavior");
}
} // namespace RzMetricCases
