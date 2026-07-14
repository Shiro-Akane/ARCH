#pragma once
// Generated only from the uploaded Timmes public_iso7.f90.
// Regenerate with scripts/translate_timmes_rates.py; do not hand-edit.
#include <algorithm>
#include <cmath>
#include "../timmes_common/RatePair.h"
#include "../timmes_common/TfactorsData.h"

namespace timmes {
struct Iso7RateLibrary {

// public_iso7.f90:17579
static inline RatePair rate_c12ag(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, dd = 0.0, ddd = 0.0, dee = 0.0, df1 = 0.0, df2 = 0.0, dff = 0.0, dgg = 0.0, dhh = 0.0, drevdt = 0.0, dtermdt = 0.0, ee = 0.0, f1 = 0.0, f2 = 0.0, ff = 0.0, gg = 0.0, hh = 0.0, rev = 0.0, term = 0.0, zz = 0.0;
    const double q1 = (1.0e0 / 12.222016e0);
    aa = (1.0e0 + (0.0489e0 * tf.t9i23));
    daa = (((-twoth) * 0.0489e0) * tf.t9i53);
    bb = ((tf.t92 * aa) * aa);
    dbb = (2.0e0 * (((bb * tf.t9i) + ((tf.t92 * aa) * daa))));
    cc = std::exp((((-32.120e0) * tf.t9i13) - (tf.t92 * q1)));
    dcc = (cc * ((((oneth * 32.120e0) * tf.t9i43) - ((2.0e0 * tf.t9) * q1))));
    dd = (1.0e0 + (0.2654e0 * tf.t9i23));
    ddd = (((-twoth) * 0.2654e0) * tf.t9i53);
    ee = ((tf.t92 * dd) * dd);
    dee = (2.0e0 * (((ee * tf.t9i) + ((tf.t92 * dd) * ddd))));
    ff = std::exp(((-32.120e0) * tf.t9i13));
    dff = (((ff * oneth) * 32.120e0) * tf.t9i43);
    gg = ((1.25e3 * tf.t9i32) * std::exp(((-static_cast<double>(27.499f)) * tf.t9i)));
    dgg = (gg * ((((-1.5e0) * tf.t9i) + (static_cast<double>(27.499f) * tf.t9i2))));
    hh = ((1.43e-2 * tf.t95) * std::exp(((-static_cast<double>(15.541f)) * tf.t9i)));
    dhh = (hh * (((5.0e0 * tf.t9i) + (static_cast<double>(15.541f) * tf.t9i2))));
    zz = (1.0e0 / bb);
    f1 = (cc * zz);
    df1 = (((dcc - (f1 * dbb))) * zz);
    zz = (1.0e0 / ee);
    f2 = (ff * zz);
    df2 = (((dff - (f2 * dee))) * zz);
    term = ((((1.04e8 * f1) + (1.76e8 * f2)) + gg) + hh);
    dtermdt = ((((1.04e8 * df1) + (1.76e8 * df2)) + dgg) + dhh);
    term = (1.7e0 * term);
    dtermdt = (1.7e0 * dtermdt);
    fr = (term * den);
    dfrdt = ((dtermdt * den) * 1.0e-9);
    dfrdd = term;
    rev = ((5.13e10 * tf.t932) * std::exp(((-static_cast<double>(83.111f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(83.111f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_iso7.f90:17653
static inline RatePair rate_tripalf(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, dd = 0.0, ddd = 0.0, dee = 0.0, df1 = 0.0, dff = 0.0, dr2abedt = 0.0, drbeacdt = 0.0, drevdt = 0.0, dtermdt = 0.0, dxx = 0.0, dyy = 0.0, dzz = 0.0, ee = 0.0, f1 = 0.0, ff = 0.0, r2abe = 0.0, rbeac = 0.0, rev = 0.0, term = 0.0, uu = 0.0, vv = 0.0, xx = 0.0, yy = 0.0, zz = 0.0;
    const double rc28 = 0.1e0;
    const double q1 = (1.0e0 / 0.009604e0);
    const double q2 = (1.0e0 / 0.055225e0);
    aa = ((7.40e+05 * tf.t9i32) * std::exp(((-static_cast<double>(1.0663f)) * tf.t9i)));
    daa = (aa * ((((-1.5e0) * tf.t9i) + (static_cast<double>(1.0663f) * tf.t9i2))));
    bb = ((4.164e+09 * tf.t9i23) * std::exp((((-static_cast<double>(13.49f)) * tf.t9i13) - (tf.t92 * q1))));
    dbb = (bb * (((((-twoth) * tf.t9i) + ((oneth * static_cast<double>(13.49f)) * tf.t9i43)) - ((2.0e0 * tf.t9) * q1))));
    cc = (((((1.0e0 + (static_cast<double>(0.031f) * tf.t913)) + (static_cast<double>(8.009f) * tf.t923)) + (static_cast<double>(1.732f) * tf.t9)) + (static_cast<double>(49.883f) * tf.t943)) + (static_cast<double>(27.426f) * tf.t953));
    dcc = ((((((oneth * static_cast<double>(0.031f)) * tf.t9i23) + ((twoth * static_cast<double>(8.009f)) * tf.t9i13)) + static_cast<double>(1.732f)) + ((fourth * static_cast<double>(49.883f)) * tf.t913)) + ((fiveth * static_cast<double>(27.426f)) * tf.t923));
    r2abe = (aa + (bb * cc));
    dr2abedt = ((daa + (dbb * cc)) + (bb * dcc));
    dd = ((130.0e0 * tf.t9i32) * std::exp(((-static_cast<double>(3.3364f)) * tf.t9i)));
    ddd = (dd * ((((-1.5e0) * tf.t9i) + (static_cast<double>(3.3364f) * tf.t9i2))));
    ee = ((2.510e+07 * tf.t9i23) * std::exp((((-static_cast<double>(23.57f)) * tf.t9i13) - (tf.t92 * q2))));
    dee = (ee * (((((-twoth) * tf.t9i) + ((oneth * static_cast<double>(23.57f)) * tf.t9i43)) - ((2.0e0 * tf.t9) * q2))));
    ff = (((((1.0e0 + (static_cast<double>(0.018f) * tf.t913)) + (static_cast<double>(5.249f) * tf.t923)) + (static_cast<double>(0.650f) * tf.t9)) + (static_cast<double>(19.176f) * tf.t943)) + (static_cast<double>(6.034f) * tf.t953));
    dff = ((((((oneth * static_cast<double>(0.018f)) * tf.t9i23) + ((twoth * static_cast<double>(5.249f)) * tf.t9i13)) + static_cast<double>(0.650f)) + ((fourth * static_cast<double>(19.176f)) * tf.t913)) + ((fiveth * static_cast<double>(6.034f)) * tf.t923));
    rbeac = (dd + (ee * ff));
    drbeacdt = ((ddd + (dee * ff)) + (ee * dff));
    xx = (((rc28 * 1.35e-07) * tf.t9i32) * std::exp(((-static_cast<double>(24.811f)) * tf.t9i)));
    dxx = (xx * ((((-1.5e0) * tf.t9i) + (static_cast<double>(24.811f) * tf.t9i2))));
    if ((tf.t9 > static_cast<double>(0.08f))) {
        term = (((2.90e-16 * r2abe) * rbeac) + xx);
        dtermdt = ((((2.90e-16 * dr2abedt) * rbeac) + ((2.90e-16 * r2abe) * drbeacdt)) + dxx);
    } else {
        uu = (0.8e0 * std::exp((-std::pow(((static_cast<double>(0.025f) * tf.t9i)), static_cast<double>(3.263f)))));
        yy = (0.2e0 + uu);
        dyy = (((uu * static_cast<double>(3.263f)) * std::pow(((static_cast<double>(0.025f) * tf.t9i)), static_cast<double>(2.263f))) * ((static_cast<double>(0.025f) * tf.t9i2)));
        vv = (4.0e0 * std::exp((-std::pow(((tf.t9 / static_cast<double>(0.025f))), static_cast<double>(9.227f)))));
        zz = (1.0e0 + vv);
        dzz = (((vv * static_cast<double>(9.227f)) * std::pow(((tf.t9 / static_cast<double>(0.025f))), static_cast<double>(8.227f))) * 40.0e0);
        aa = (1.0e0 / zz);
        f1 = (0.01e0 + (yy * aa));
        df1 = (((dyy - (f1 * dzz))) * aa);
        term = ((((2.90e-16 * r2abe) * rbeac) * f1) + xx);
        dtermdt = ((((((2.90e-16 * dr2abedt) * rbeac) * f1) + (((2.90e-16 * r2abe) * drbeacdt) * f1)) + (((2.90e-16 * r2abe) * rbeac) * df1)) + dxx);
    }
    fr = ((term * den) * den);
    dfrdt = (((dtermdt * den) * den) * 1.0e-9);
    dfrdd = ((2.0e0 * term) * den);
    rev = ((2.00e+20 * tf.t93) * std::exp(((-static_cast<double>(84.424f)) * tf.t9i)));
    drevdt = (rev * (((3.0e0 * tf.t9i) + (static_cast<double>(84.424f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_iso7.f90:17763
static inline RatePair rate_c12c12(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, dt9a = 0.0, dt9a13 = 0.0, dt9a56 = 0.0, dtermdt = 0.0, t9a = 0.0, t9a13 = 0.0, t9a56 = 0.0, term = 0.0, zz = 0.0;
    aa = (1.0e0 + (static_cast<double>(0.0396f) * tf.t9));
    zz = (1.0e0 / aa);
    t9a = (tf.t9 * zz);
    dt9a = (((1.0e0 - (t9a * static_cast<double>(0.0396f)))) * zz);
    zz = (dt9a / t9a);
    t9a13 = std::pow(t9a, oneth);
    dt9a13 = ((oneth * t9a13) * zz);
    t9a56 = std::pow(t9a, fivsix);
    dt9a56 = ((fivsix * t9a56) * zz);
    term = (((4.27e+26 * t9a56) * tf.t9i32) * std::exp((((-static_cast<double>(84.165f)) / t9a13) - (2.12e-03 * tf.t93))));
    dtermdt = (term * (((((dt9a56 / t9a56) - (1.5e0 * tf.t9i)) + ((static_cast<double>(84.165f) / std::pow(t9a13, 2)) * dt9a13)) - (6.36e-3 * tf.t92))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rr = 0.0e0;
    drrdt = 0.0e0;
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_iso7.f90:17960
static inline RatePair rate_c12o16(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, dt9a = 0.0, dt9a13 = 0.0, dt9a23 = 0.0, dt9a56 = 0.0, dtermdt = 0.0, t9a = 0.0, t9a13 = 0.0, t9a23 = 0.0, t9a56 = 0.0, term = 0.0, zz = 0.0;
    if ((tf.t9 >= static_cast<double>(0.5f))) {
        aa = (1.0e0 + (static_cast<double>(0.055f) * tf.t9));
        zz = (1.0e0 / aa);
        t9a = (tf.t9 * zz);
        dt9a = (((1.0e0 - (t9a * static_cast<double>(0.055f)))) * zz);
        zz = (dt9a / t9a);
        t9a13 = std::pow(t9a, oneth);
        dt9a13 = ((oneth * t9a13) * zz);
        t9a23 = (t9a13 * t9a13);
        dt9a23 = ((2.0e0 * t9a13) * dt9a13);
        t9a56 = std::pow(t9a, fivsix);
        dt9a56 = ((fivsix * t9a56) * zz);
        aa = std::exp((((-static_cast<double>(0.18f)) * t9a) * t9a));
        daa = ((((-aa) * static_cast<double>(0.36f)) * t9a) * dt9a);
        bb = (1.06e-03 * std::exp((static_cast<double>(2.562f) * t9a23)));
        dbb = ((bb * static_cast<double>(2.562f)) * dt9a23);
        cc = (aa + bb);
        dcc = (daa + dbb);
        zz = (1.0e0 / cc);
        term = ((((1.72e+31 * t9a56) * tf.t9i32) * std::exp(((-static_cast<double>(106.594f)) / t9a13))) * zz);
        dtermdt = (term * (((((dt9a56 / t9a56) - (1.5e0 * tf.t9i)) + ((static_cast<double>(106.594f) / t9a23) * dt9a13)) - (zz * dcc))));
    } else {
        term = 0.0e0;
        dtermdt = 0.0e0;
    }
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rr = 0.0e0;
    drrdt = 0.0e0;
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_iso7.f90:18162
static inline RatePair rate_o16o16(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double dtermdt = 0.0, term = 0.0;
    term = ((7.10e36 * tf.t9i23) * std::exp((((((-static_cast<double>(135.93f)) * tf.t9i13) - (static_cast<double>(0.629f) * tf.t923)) - (static_cast<double>(0.445f) * tf.t943)) + ((static_cast<double>(0.0103f) * tf.t9) * tf.t9))));
    dtermdt = ((((-twoth) * term) * tf.t9i) + (term * ((((((oneth * static_cast<double>(135.93f)) * tf.t9i43) - ((twoth * static_cast<double>(0.629f)) * tf.t9i13)) - ((fourth * static_cast<double>(0.445f)) * tf.t913)) + (static_cast<double>(0.0206f) * tf.t9)))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rr = 0.0e0;
    drrdt = 0.0e0;
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_iso7.f90:18391
static inline RatePair rate_o16ag(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, drevdt = 0.0, dterm1 = 0.0, dterm2 = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, term1 = 0.0, term2 = 0.0;
    const double q1 = (1.0e0 / 2.515396e0);
    term1 = ((9.37e9 * tf.t9i23) * std::exp((((-static_cast<double>(39.757f)) * tf.t9i13) - (tf.t92 * q1))));
    dterm1 = (term1 * (((((-twoth) * tf.t9i) + ((oneth * static_cast<double>(39.757f)) * tf.t9i43)) - ((2.0e0 * tf.t9) * q1))));
    aa = ((static_cast<double>(62.1f) * tf.t9i32) * std::exp(((-static_cast<double>(10.297f)) * tf.t9i)));
    daa = (aa * ((((-1.5e0) * tf.t9i) + (static_cast<double>(10.297f) * tf.t9i2))));
    bb = ((538.0e0 * tf.t9i32) * std::exp(((-static_cast<double>(12.226f)) * tf.t9i)));
    dbb = (bb * ((((-1.5e0) * tf.t9i) + (static_cast<double>(12.226f) * tf.t9i2))));
    cc = ((13.0e0 * tf.t92) * std::exp(((-static_cast<double>(20.093f)) * tf.t9i)));
    dcc = (cc * (((2.0e0 * tf.t9i) + (static_cast<double>(20.093f) * tf.t9i2))));
    term2 = ((aa + bb) + cc);
    dterm2 = ((daa + dbb) + dcc);
    term = (term1 + term2);
    dtermdt = (dterm1 + dterm2);
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((5.65e+10 * tf.t932) * std::exp(((-static_cast<double>(54.937f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(54.937f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_iso7.f90:18444
static inline RatePair rate_ne20ag(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, daa = 0.0, dbb = 0.0, drevdt = 0.0, dterm1 = 0.0, dterm2 = 0.0, dterm3 = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, term1 = 0.0, term2 = 0.0, term3 = 0.0, zz = 0.0;
    const double rc102 = 0.1e0;
    const double q1 = (1.0e0 / 4.923961e0);
    aa = ((4.11e+11 * tf.t9i23) * std::exp((((-static_cast<double>(46.766f)) * tf.t9i13) - (tf.t92 * q1))));
    daa = (aa * (((((-twoth) * tf.t9i) + ((oneth * static_cast<double>(46.766f)) * tf.t9i43)) - ((2.0e0 * tf.t9) * q1))));
    bb = (((((1.0e0 + (static_cast<double>(0.009f) * tf.t913)) + (static_cast<double>(0.882f) * tf.t923)) + (static_cast<double>(0.055f) * tf.t9)) + (static_cast<double>(0.749f) * tf.t943)) + (static_cast<double>(0.119f) * tf.t953));
    dbb = ((((((oneth * static_cast<double>(0.009f)) * tf.t9i23) + ((twoth * static_cast<double>(0.882f)) * tf.t9i13)) + static_cast<double>(0.055f)) + ((fourth * static_cast<double>(0.749f)) * tf.t913)) + ((fiveth * static_cast<double>(0.119f)) * tf.t923));
    term1 = (aa * bb);
    dterm1 = ((daa * bb) + (aa * dbb));
    aa = ((5.27e+03 * tf.t9i32) * std::exp(((-static_cast<double>(15.869f)) * tf.t9i)));
    daa = (aa * ((((-1.5e0) * tf.t9i) + (static_cast<double>(15.869f) * tf.t9i2))));
    bb = ((6.51e+03 * tf.t912) * std::exp(((-static_cast<double>(16.223f)) * tf.t9i)));
    dbb = (bb * (((0.5e0 * tf.t9i) + (static_cast<double>(16.223f) * tf.t9i2))));
    term2 = (aa + bb);
    dterm2 = (daa + dbb);
    aa = ((static_cast<double>(42.1f) * tf.t9i32) * std::exp(((-static_cast<double>(9.115f)) * tf.t9i)));
    daa = (aa * ((((-1.5e0) * tf.t9i) + (static_cast<double>(9.115f) * tf.t9i2))));
    bb = ((static_cast<double>(32.0f) * tf.t9i23) * std::exp(((-static_cast<double>(9.383f)) * tf.t9i)));
    dbb = (bb * ((((-twoth) * tf.t9i) + (static_cast<double>(9.383f) * tf.t9i2))));
    term3 = (rc102 * ((aa + bb)));
    dterm3 = (rc102 * ((daa + dbb)));
    aa = (5.0e0 * std::exp(((-static_cast<double>(18.960f)) * tf.t9i)));
    daa = ((aa * static_cast<double>(18.960f)) * tf.t9i2);
    bb = (1.0e0 + aa);
    dbb = daa;
    zz = (1.0e0 / bb);
    term = ((((term1 + term2) + term3)) * zz);
    dtermdt = ((((((dterm1 + dterm2) + dterm3)) - (term * dbb))) * zz);
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((6.01e+10 * tf.t932) * std::exp(((-static_cast<double>(108.059f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(108.059f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_iso7.f90:18524
static inline RatePair rate_mg24ag(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, dd = 0.0, ddd = 0.0, dee = 0.0, dff = 0.0, dgg = 0.0, drevdt = 0.0, dtermdt = 0.0, ee = 0.0, ff = 0.0, gg = 0.0, hh = 0.0, hhi = 0.0, rev = 0.0, term = 0.0;
    const double rc121 = 0.1e0;
    aa = ((4.78e+01 * tf.t9i32) * std::exp(((-static_cast<double>(13.506f)) * tf.t9i)));
    daa = (aa * ((((-1.5e0) * tf.t9i) + (static_cast<double>(13.506f) * tf.t9i2))));
    bb = ((2.38e+03 * tf.t9i32) * std::exp(((-static_cast<double>(15.218f)) * tf.t9i)));
    dbb = (bb * ((((-1.5e0) * tf.t9i) + (static_cast<double>(15.218f) * tf.t9i2))));
    cc = ((2.47e+02 * tf.t932) * std::exp(((-static_cast<double>(15.147f)) * tf.t9i)));
    dcc = (cc * (((1.5e0 * tf.t9i) + (static_cast<double>(15.147f) * tf.t9i2))));
    dd = (((rc121 * 1.72e-09) * tf.t9i32) * std::exp(((-static_cast<double>(5.028f)) * tf.t9i)));
    ddd = (dd * ((((-1.5e0) * tf.t9i) + (static_cast<double>(5.028f) * tf.t9i2))));
    ee = (((rc121 * 1.25e-03) * tf.t9i32) * std::exp(((-static_cast<double>(7.929f)) * tf.t9i)));
    dee = (ee * ((((-1.5e0) * tf.t9i) + (static_cast<double>(7.929f) * tf.t9i2))));
    ff = (((rc121 * 2.43e+01) * tf.t9i) * std::exp(((-static_cast<double>(11.523f)) * tf.t9i)));
    dff = (ff * (((-tf.t9i) + (static_cast<double>(11.523f) * tf.t9i2))));
    gg = (5.0e0 * std::exp(((-static_cast<double>(15.882f)) * tf.t9i)));
    dgg = ((gg * static_cast<double>(15.882f)) * tf.t9i2);
    hh = (1.0e0 + gg);
    hhi = (1.0e0 / hh);
    term = (((((((aa + bb) + cc) + dd) + ee) + ff)) * hhi);
    dtermdt = ((((((((daa + dbb) + dcc) + ddd) + dee) + dff) - (term * dgg))) * hhi);
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((6.27e+10 * tf.t932) * std::exp(((-static_cast<double>(115.862f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(115.862f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_iso7.f90:19212
static inline RatePair rate_ca40ag(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (1.650e-2 * z)) + (5.973e-3 * z2)) - (3.889e-04 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((1.650e-2 + ((2.0e0 * 5.973e-3) * z)) - ((3.0e0 * 3.889e-4) * z2));
    }
    term = ((4.66e+24 * tf.t9i23) * std::exp((((-static_cast<double>(76.435f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(76.435f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((6.843e+10 * tf.t932) * std::exp(((-static_cast<double>(59.510f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(59.510f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

}; // struct Iso7RateLibrary
} // namespace timmes
