#pragma once
// Generated only from the uploaded Timmes public_aprox21.f90.
// Regenerate with scripts/translate_timmes_rates.py; do not hand-edit.
#include <algorithm>
#include <cmath>
#include "../timmes_common/RatePair.h"
#include "../timmes_common/TfactorsData.h"

namespace timmes {
struct Aprox21RateLibrary {

// public_aprox21.f90:21024
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

// public_aprox21.f90:20950
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

// public_aprox21.f90:21134
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

// public_aprox21.f90:21331
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

// public_aprox21.f90:21533
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

// public_aprox21.f90:21762
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

// public_aprox21.f90:21815
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

// public_aprox21.f90:21895
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

// public_aprox21.f90:21958
static inline RatePair rate_mg24ap(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, dd = 0.0, ddd = 0.0, dee = 0.0, dff = 0.0, dgg = 0.0, drevdt = 0.0, dterm1 = 0.0, dterm2 = 0.0, dtermdt = 0.0, ee = 0.0, ff = 0.0, gg = 0.0, rev = 0.0, term = 0.0, term1 = 0.0, term2 = 0.0;
    const double rc148 = 0.1e0;
    const double q1 = (1.0e0 / 0.024649e0);
    aa = ((1.10e+08 * tf.t9i23) * std::exp((((-static_cast<double>(23.261f)) * tf.t9i13) - (tf.t92 * q1))));
    daa = ((((-twoth) * aa) * tf.t9i) + (aa * (((static_cast<double>(23.261f) * tf.t9i43) - ((2.0e0 * tf.t9) * q1)))));
    bb = (((((1.0e0 + (static_cast<double>(0.018f) * tf.t913)) + (static_cast<double>(12.85f) * tf.t923)) + (static_cast<double>(1.61f) * tf.t9)) + (static_cast<double>(89.87f) * tf.t943)) + (static_cast<double>(28.66f) * tf.t953));
    dbb = ((((((oneth * static_cast<double>(0.018f)) * tf.t9i23) + ((twoth * static_cast<double>(12.85f)) * tf.t9i13)) + static_cast<double>(1.61f)) + ((fourth * static_cast<double>(89.87f)) * tf.t913)) + ((fiveth * static_cast<double>(28.66f)) * tf.t923));
    term1 = (aa * bb);
    dterm1 = ((daa * bb) + (aa * dbb));
    aa = ((129.0e0 * tf.t9i32) * std::exp(((-static_cast<double>(2.517f)) * tf.t9i)));
    daa = ((((-1.5e0) * aa) * tf.t9i) + ((aa * static_cast<double>(2.517f)) * tf.t9i2));
    bb = ((5660.0e0 * tf.t972) * std::exp(((-static_cast<double>(3.421f)) * tf.t9i)));
    dbb = (((3.5e0 * bb) * tf.t9i) + ((bb * static_cast<double>(3.421f)) * tf.t9i2));
    cc = (((rc148 * 3.89e-08) * tf.t9i32) * std::exp(((-static_cast<double>(0.853f)) * tf.t9i)));
    dcc = ((((-1.5e0) * cc) * tf.t9i) + ((cc * static_cast<double>(0.853f)) * tf.t9i2));
    dd = (((rc148 * 8.18e-09) * tf.t9i32) * std::exp(((-static_cast<double>(1.001f)) * tf.t9i)));
    ddd = ((((-1.5e0) * dd) * tf.t9i) + ((dd * static_cast<double>(1.001f)) * tf.t9i2));
    term2 = (((aa + bb) + cc) + dd);
    dterm2 = (((daa + dbb) + dcc) + ddd);
    ee = (oneth * std::exp(((-static_cast<double>(9.792f)) * tf.t9i)));
    dee = ((ee * static_cast<double>(9.792f)) * tf.t9i2);
    ff = (twoth * std::exp(((-static_cast<double>(11.773f)) * tf.t9i)));
    dff = ((ff * static_cast<double>(11.773f)) * tf.t9i2);
    gg = ((1.0e0 + ee) + ff);
    dgg = (dee + dff);
    term = (((term1 + term2)) / gg);
    dtermdt = (((((dterm1 + dterm2)) - (term * dgg))) / gg);
    rev = (static_cast<double>(1.81f) * std::exp(((-static_cast<double>(18.572f)) * tf.t9i)));
    drevdt = ((rev * static_cast<double>(18.572f)) * tf.t9i2);
    fr = ((den * rev) * term);
    dfrdt = ((den * (((drevdt * term) + (rev * dtermdt)))) * 1.0e-9);
    dfrdd = (rev * term);
    rr = (den * term);
    drrdt = ((den * dtermdt) * 1.0e-9);
    drrdd = term;
    return {fr, rr};
}

// public_aprox21.f90:22035
static inline RatePair rate_al27pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, dd = 0.0, ddd = 0.0, dee = 0.0, dff = 0.0, dgg = 0.0, drevdt = 0.0, dtermdt = 0.0, ee = 0.0, ff = 0.0, gg = 0.0, rev = 0.0, term = 0.0;
    aa = ((1.32e+09 * tf.t9i23) * std::exp(((-static_cast<double>(23.26f)) * tf.t9i13)));
    daa = (aa * ((((-twoth) * tf.t9i) + ((oneth * static_cast<double>(23.26f)) * tf.t9i43))));
    bb = (((3.22e-10 * tf.t9i32) * std::exp(((-static_cast<double>(0.836f)) * tf.t9i))) * static_cast<double>(0.17f));
    dbb = (bb * ((((-1.5e0) * tf.t9i) + (static_cast<double>(0.836f) * tf.t9i2))));
    cc = ((1.74e+00 * tf.t9i32) * std::exp(((-static_cast<double>(2.269f)) * tf.t9i)));
    dcc = (cc * ((((-1.5e0) * tf.t9i) + (static_cast<double>(2.269f) * tf.t9i2))));
    dd = ((9.92e+00 * tf.t9i32) * std::exp(((-static_cast<double>(2.492f)) * tf.t9i)));
    ddd = (dd * ((((-1.5e0) * tf.t9i) + (static_cast<double>(2.492f) * tf.t9i2))));
    ee = ((4.29e+01 * tf.t9i32) * std::exp(((-static_cast<double>(3.273f)) * tf.t9i)));
    dee = (ee * ((((-1.5e0) * tf.t9i) + (static_cast<double>(3.273f) * tf.t9i2))));
    ff = ((1.34e+02 * tf.t9i32) * std::exp(((-static_cast<double>(3.654f)) * tf.t9i)));
    dff = (ff * ((((-1.5e0) * tf.t9i) + (static_cast<double>(3.654f) * tf.t9i2))));
    gg = ((1.77e+04 * (std::pow(tf.t9, static_cast<double>(0.53f)))) * std::exp(((-static_cast<double>(4.588f)) * tf.t9i)));
    dgg = (gg * (((static_cast<double>(0.53f) * tf.t9i) + (static_cast<double>(4.588f) * tf.t9i2))));
    term = ((((((aa + bb) + cc) + dd) + ee) + ff) + gg);
    dtermdt = ((((((daa + dbb) + dcc) + ddd) + dee) + dff) + dgg);
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((1.13e+11 * tf.t932) * std::exp(((-static_cast<double>(134.434f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(134.434f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:22175
static inline RatePair rate_si28ag(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (6.340e-2 * z)) + (2.541e-3 * z2)) - (2.900e-4 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0;
    } else {
        daa = ((6.340e-2 + ((2.0e0 * 2.541e-3) * tf.t9)) - ((3.0e0 * 2.900e-4) * tf.t92));
    }
    term = ((4.82e+22 * tf.t9i23) * std::exp((((-static_cast<double>(61.015f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(61.015f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((6.461e+10 * tf.t932) * std::exp(((-static_cast<double>(80.643f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(80.643f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:22220
static inline RatePair rate_si28ap(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (2.798e-3 * z)) + (2.763e-3 * z2)) - (2.341e-4 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((2.798e-3 + ((2.0e0 * 2.763e-3) * tf.t9)) - ((3.0e0 * 2.341e-4) * tf.t92));
    }
    term = ((4.16e+13 * tf.t9i23) * std::exp((((-static_cast<double>(25.631f)) * tf.t9i13) * aa)));
    dtermdt = ((((-twoth) * term) * tf.t9i) + (((term * static_cast<double>(25.631f)) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))));
    rev = (0.5825e0 * std::exp(((-static_cast<double>(22.224f)) * tf.t9i)));
    drevdt = ((rev * static_cast<double>(22.224f)) * tf.t9i2);
    fr = ((den * rev) * term);
    dfrdt = ((den * (((drevdt * term) + (rev * dtermdt)))) * 1.0e-9);
    dfrdd = (rev * term);
    rr = (den * term);
    drrdt = ((den * dtermdt) * 1.0e-9);
    drrdd = term;
    return {fr, rr};
}

// public_aprox21.f90:22266
static inline RatePair rate_p31pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (1.928e-1 * z)) - (1.540e-2 * z2)) + (6.444e-4 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((1.928e-1 - ((2.0e0 * 1.540e-2) * tf.t9)) + ((3.0e0 * 6.444e-4) * tf.t92));
    }
    term = ((1.08e+16 * tf.t9i23) * std::exp((((-static_cast<double>(27.042f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(27.042f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((3.764e+10 * tf.t932) * std::exp(((-static_cast<double>(102.865f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(102.865f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:22312
static inline RatePair rate_s32ag(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (4.913e-2 * z)) + (4.637e-3 * z2)) - (4.067e-4 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((4.913e-2 + ((2.0e0 * 4.637e-3) * tf.t9)) - ((3.0e0 * 4.067e-4) * tf.t92));
    }
    term = ((1.16e+24 * tf.t9i23) * std::exp((((-static_cast<double>(66.690f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(66.690f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((6.616e+10 * tf.t932) * std::exp(((-static_cast<double>(77.080f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(77.080f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:22358
static inline RatePair rate_s32ap(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (1.041e-1 * z)) - (1.368e-2 * z2)) + (6.969e-4 * z3));
    if ((z == 10)) {
        daa = 0.0e0;
    } else {
        daa = ((1.041e-1 - ((2.0e0 * 1.368e-2) * tf.t9)) + ((3.0e0 * 6.969e-4) * tf.t92));
    }
    term = ((1.27e+16 * tf.t9i23) * std::exp((((-static_cast<double>(31.044f)) * tf.t9i13) * aa)));
    dtermdt = ((((-twoth) * term) * tf.t9i) + (((term * static_cast<double>(31.044f)) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))));
    rev = (static_cast<double>(1.144f) * std::exp(((-static_cast<double>(21.643f)) * tf.t9i)));
    drevdt = ((rev * static_cast<double>(21.643f)) * tf.t9i2);
    fr = ((den * rev) * term);
    dfrdt = ((den * (((drevdt * term) + (rev * dtermdt)))) * 1.0e-9);
    dfrdd = (rev * term);
    rr = (den * term);
    drrdt = ((den * dtermdt) * 1.0e-9);
    drrdd = term;
    return {fr, rr};
}

// public_aprox21.f90:22404
static inline RatePair rate_cl35pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0;
    aa = (((1.0e0 + (1.761e-1 * tf.t9)) - (1.322e-2 * tf.t92)) + (5.245e-4 * tf.t93));
    daa = ((1.761e-1 - ((2.0e0 * 1.322e-2) * tf.t9)) + ((3.0e0 * 5.245e-4) * tf.t92));
    term = ((4.48e+16 * tf.t9i23) * std::exp((((-static_cast<double>(29.483f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(29.483f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((7.568e+10 * tf.t932) * std::exp(((-static_cast<double>(98.722f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(98.722f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:22445
static inline RatePair rate_ar36ag(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (1.458e-1 * z)) - (1.069e-2 * z2)) + (3.790e-4 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((1.458e-1 - ((2.0e0 * 1.069e-2) * tf.t9)) + ((3.0e0 * 3.790e-4) * tf.t92));
    }
    term = ((2.81e+30 * tf.t9i23) * std::exp((((-static_cast<double>(78.271f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(78.271f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((6.740e+10 * tf.t932) * std::exp(((-static_cast<double>(81.711f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(81.711f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:22491
static inline RatePair rate_ar36ap(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (4.826e-3 * z)) - (5.534e-3 * z2)) + (4.021e-4 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((4.826e-3 - ((2.0e0 * 5.534e-3) * tf.t9)) + ((3.0e0 * 4.021e-4) * tf.t92));
    }
    term = ((2.76e+13 * tf.t9i23) * std::exp((((-static_cast<double>(34.922f)) * tf.t9i13) * aa)));
    dtermdt = ((((-twoth) * term) * tf.t9i) + (((term * static_cast<double>(34.922f)) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))));
    rev = (static_cast<double>(1.128f) * std::exp(((-static_cast<double>(14.959f)) * tf.t9i)));
    drevdt = ((rev * static_cast<double>(14.959f)) * tf.t9i2);
    fr = ((den * rev) * term);
    dfrdt = ((den * (((drevdt * term) + (rev * dtermdt)))) * 1.0e-9);
    dfrdd = (rev * term);
    rr = (den * term);
    drrdt = ((den * dtermdt) * 1.0e-9);
    drrdd = term;
    return {fr, rr};
}

// public_aprox21.f90:22537
static inline RatePair rate_k39pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (1.622e-1 * z)) - (1.119e-2 * z2)) + (3.910e-4 * z3));
    if ((z == 10)) {
        daa = 0.0e0;
    } else {
        daa = ((1.622e-1 - ((2.0e0 * 1.119e-2) * tf.t9)) + ((3.0e0 * 3.910e-4) * tf.t92));
    }
    term = ((4.09e+16 * tf.t9i23) * std::exp((((-static_cast<double>(31.727f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(31.727f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((7.600e+10 * tf.t932) * std::exp(((-static_cast<double>(96.657f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(96.657f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:22583
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

// public_aprox21.f90:22629
static inline RatePair rate_ca40ap(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 - (1.206e-2 * z)) + (7.753e-3 * z2)) - (5.071e-4 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = (((-1.206e-2) + ((2.0e0 * 7.753e-3) * tf.t9)) - ((3.0e0 * 5.071e-4) * tf.t92));
    }
    term = ((4.54e+14 * tf.t9i23) * std::exp((((-static_cast<double>(32.177f)) * tf.t9i13) * aa)));
    dtermdt = ((((-twoth) * term) * tf.t9i) + (((term * static_cast<double>(32.177f)) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))));
    rev = (static_cast<double>(2.229f) * std::exp(((-static_cast<double>(40.966f)) * tf.t9i)));
    drevdt = ((rev * static_cast<double>(40.966f)) * tf.t9i2);
    fr = ((den * rev) * term);
    dfrdt = ((den * (((drevdt * term) + (rev * dtermdt)))) * 1.0e-9);
    dfrdd = (rev * term);
    rr = (den * term);
    drrdt = ((den * dtermdt) * 1.0e-9);
    drrdd = term;
    return {fr, rr};
}

// public_aprox21.f90:22675
static inline RatePair rate_sc43pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (1.023e-1 * z)) - (2.242e-3 * z2)) - (5.463e-5 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((1.023e-1 - ((2.0e0 * 2.242e-3) * tf.t9)) - ((3.0e0 * 5.463e-5) * tf.t92));
    }
    term = ((3.85e+16 * tf.t9i23) * std::exp((((-static_cast<double>(33.234f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(33.234f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((1.525e+11 * tf.t932) * std::exp(((-static_cast<double>(100.475f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(100.475f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:22722
static inline RatePair rate_ti44ag(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (1.066e-1 * z)) - (1.102e-2 * z2)) + (5.324e-4 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((1.066e-1 - ((2.0e0 * 1.102e-2) * tf.t9)) + ((3.0e0 * 5.324e-4) * tf.t92));
    }
    term = ((1.37e+26 * tf.t9i23) * std::exp((((-static_cast<double>(81.227f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(81.227f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((6.928e+10 * tf.t932) * std::exp(((-static_cast<double>(89.289f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(89.289f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:22768
static inline RatePair rate_ti44ap(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (2.655e-2 * z)) - (3.947e-3 * z2)) + (2.522e-4 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((2.655e-2 - ((2.0e0 * 3.947e-3) * tf.t9)) + ((3.0e0 * 2.522e-4) * tf.t92));
    }
    term = ((6.54e+20 * tf.t9i23) * std::exp((((-static_cast<double>(66.678f)) * tf.t9i13) * aa)));
    dtermdt = ((((-twoth) * term) * tf.t9i) + (((term * static_cast<double>(66.678f)) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))));
    rev = (static_cast<double>(1.104f) * std::exp(((-static_cast<double>(4.723f)) * tf.t9i)));
    drevdt = ((rev * static_cast<double>(4.723f)) * tf.t9i2);
    fr = ((den * rev) * term);
    dfrdt = ((den * (((drevdt * term) + (rev * dtermdt)))) * 1.0e-9);
    dfrdd = (rev * term);
    rr = (den * term);
    drrdt = ((den * dtermdt) * 1.0e-9);
    drrdd = term;
    return {fr, rr};
}

// public_aprox21.f90:22818
static inline RatePair rate_v47pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (9.979e-2 * z)) - (2.269e-3 * z2)) - (6.662e-5 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((9.979e-2 - ((2.0e0 * 2.269e-3) * tf.t9)) - ((3.0e0 * 6.662e-5) * tf.t92));
    }
    term = ((2.05e+17 * tf.t9i23) * std::exp((((-static_cast<double>(35.568f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(35.568f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((7.649e+10 * tf.t932) * std::exp(((-static_cast<double>(93.999f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(93.999f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:22869
static inline RatePair rate_cr48ag(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (6.325e-2 * z)) - (5.671e-3 * z2)) + (2.848e-4 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((6.325e-2 - ((2.0e0 * 5.671e-3) * tf.t9)) + ((3.0e0 * 2.848e-4) * tf.t92));
    }
    term = ((1.04e+23 * tf.t9i23) * std::exp((((-static_cast<double>(81.420f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(81.420f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((7.001e+10 * tf.t932) * std::exp(((-static_cast<double>(92.177f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(92.177f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:22919
static inline RatePair rate_cr48ap(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (1.384e-2 * z)) + (1.081e-3 * z2)) - (5.933e-5 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((1.384e-2 + ((2.0e0 * 1.081e-3) * tf.t9)) - ((3.0e0 * 5.933e-5) * tf.t92));
    }
    term = ((1.83e+26 * tf.t9i23) * std::exp((((-static_cast<double>(86.741f)) * tf.t9i13) * aa)));
    dtermdt = ((((-twoth) * term) * tf.t9i) + (((term * static_cast<double>(86.741f)) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = (static_cast<double>(0.6087f) * std::exp(((-static_cast<double>(6.510f)) * tf.t9i)));
    drevdt = ((rev * static_cast<double>(6.510f)) * tf.t9i2);
    rr = ((den * rev) * term);
    drrdt = ((den * (((drevdt * term) + (rev * dtermdt)))) * 1.0e-9);
    drrdd = (rev * term);
    return {fr, rr};
}

// public_aprox21.f90:22969
static inline RatePair rate_mn51pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (8.922e-2 * z)) - (1.256e-3 * z2)) - (9.453e-5 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((8.922e-2 - ((2.0e0 * 1.256e-3) * tf.t9)) - ((3.0e0 * 9.453e-5) * tf.t92));
    }
    term = ((3.77e+17 * tf.t9i23) * std::exp((((-static_cast<double>(37.516f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(37.516f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((1.150e+11 * tf.t932) * std::exp(((-static_cast<double>(85.667f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(85.667f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:23020
static inline RatePair rate_fe52ag(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (7.846e-2 * z)) - (7.430e-3 * z2)) + (3.723e-4 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((7.846e-2 - ((2.0e0 * 7.430e-3) * tf.t9)) + ((3.0e0 * 3.723e-4) * tf.t92));
    }
    term = ((1.05e+27 * tf.t9i23) * std::exp((((-static_cast<double>(91.674f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(91.674f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((7.064e+10 * tf.t932) * std::exp(((-static_cast<double>(92.850f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(92.850f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:23070
static inline RatePair rate_fe52ap(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (1.367e-2 * z)) + (7.428e-4 * z2)) - (3.050e-5 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((1.367e-2 + ((2.0e0 * 7.428e-4) * tf.t9)) - ((3.0e0 * 3.050e-5) * tf.t92));
    }
    term = ((1.30e+27 * tf.t9i23) * std::exp((((-static_cast<double>(91.674f)) * tf.t9i13) * aa)));
    dtermdt = ((((-twoth) * term) * tf.t9i) + (((term * static_cast<double>(91.674f)) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = (static_cast<double>(0.4597f) * std::exp(((-static_cast<double>(9.470f)) * tf.t9i)));
    drevdt = ((rev * static_cast<double>(9.470f)) * tf.t9i2);
    rr = ((den * rev) * term);
    drrdt = ((den * (((drevdt * term) + (rev * dtermdt)))) * 1.0e-9);
    drrdd = (rev * term);
    return {fr, rr};
}

// public_aprox21.f90:23120
static inline RatePair rate_co55pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (9.894e-2 * z)) - (3.131e-3 * z2)) - (2.160e-5 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((9.894e-2 - ((2.0e0 * 3.131e-3) * tf.t9)) - ((3.0e0 * 2.160e-5) * tf.t92));
    }
    term = ((1.21e+18 * tf.t9i23) * std::exp((((-static_cast<double>(39.604f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(39.604f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((1.537e+11 * tf.t932) * std::exp(((-static_cast<double>(83.382f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(83.382f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:18964
static inline RatePair rate_pp(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, daa = 0.0, dbb = 0.0, dtermdt = 0.0, term = 0.0;
    if ((tf.t9 <= static_cast<double>(3.0f))) {
        aa = ((4.01e-15 * tf.t9i23) * std::exp(((-3.380e0) * tf.t9i13)));
        daa = (aa * ((((-twoth) * tf.t9i) + ((oneth * 3.380e0) * tf.t9i43))));
        bb = (((1.0e0 + (0.123e0 * tf.t913)) + (1.09e0 * tf.t923)) + (0.938e0 * tf.t9));
        dbb = ((((oneth * 0.123e0) * tf.t9i23) + ((twoth * 1.09e0) * tf.t9i13)) + 0.938e0);
        term = (aa * bb);
        dtermdt = ((daa * bb) + (aa * dbb));
    } else {
        term = 1.1581136e-15;
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

// public_aprox21.f90:19095
static inline RatePair rate_png(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0;
    aa = (((((1.0e0 - (static_cast<double>(0.8504f) * tf.t912)) + (static_cast<double>(0.4895f) * tf.t9)) - (static_cast<double>(0.09623f) * tf.t932)) + (static_cast<double>(8.471e-3f) * tf.t92)) - (static_cast<double>(2.80e-4f) * tf.t952));
    daa = (((((((-0.5e0) * static_cast<double>(0.8504f)) * tf.t9i12) + static_cast<double>(0.4895f)) - ((1.5e0 * static_cast<double>(0.09623f)) * tf.t912)) + ((2.0e0 * static_cast<double>(8.471e-3f)) * tf.t9)) - ((2.5e0 * static_cast<double>(2.80e-4f)) * tf.t932));
    term = (static_cast<double>(4.742e4f) * aa);
    dtermdt = (static_cast<double>(4.742e4f) * daa);
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((4.71e+09 * tf.t932) * std::exp(((-static_cast<double>(25.82f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(25.82f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:19150
static inline RatePair rate_dpg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, daa = 0.0, dbb = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0;
    aa = ((2.24e+03 * tf.t9i23) * std::exp(((-static_cast<double>(3.720f)) * tf.t9i13)));
    daa = (aa * ((((-twoth) * tf.t9i) + ((oneth * static_cast<double>(3.720f)) * tf.t9i43))));
    bb = (((1.0e0 + (static_cast<double>(0.112f) * tf.t913)) + (static_cast<double>(3.38f) * tf.t923)) + (static_cast<double>(2.65f) * tf.t9));
    dbb = ((((oneth * static_cast<double>(0.112f)) * tf.t9i23) + ((twoth * static_cast<double>(3.38f)) * tf.t9i13)) + static_cast<double>(2.65f));
    term = (aa * bb);
    dtermdt = ((daa * bb) + (aa * dbb));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((1.63e+10 * tf.t932) * std::exp(((-static_cast<double>(63.750f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(63.750f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:19190
static inline RatePair rate_he3ng(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0;
    term = (static_cast<double>(6.62f) * ((1.0e0 + (static_cast<double>(905.0f) * tf.t9))));
    dtermdt = 5.9911e3;
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((2.61e+10 * tf.t932) * std::exp(((-static_cast<double>(238.81f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(238.81f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:19224
static inline RatePair rate_he3he3(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, daa = 0.0, dbb = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0;
    aa = ((6.04e+10 * tf.t9i23) * std::exp(((-static_cast<double>(12.276f)) * tf.t9i13)));
    daa = (aa * ((((-twoth) * tf.t9i) + ((oneth * static_cast<double>(12.276f)) * tf.t9i43))));
    bb = (((((1.0e0 + (static_cast<double>(0.034f) * tf.t913)) - (static_cast<double>(0.522f) * tf.t923)) - (static_cast<double>(0.124f) * tf.t9)) + (static_cast<double>(0.353f) * tf.t943)) + (static_cast<double>(0.213f) * tf.t953));
    dbb = ((((((oneth * static_cast<double>(0.034f)) * tf.t9i23) - ((twoth * static_cast<double>(0.522f)) * tf.t9i13)) - static_cast<double>(0.124f)) + ((fourth * static_cast<double>(0.353f)) * tf.t913)) + ((fiveth * static_cast<double>(0.213f)) * tf.t923));
    term = (aa * bb);
    dtermdt = ((daa * bb) + (aa * dbb));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((static_cast<double>(3.39e-10f) * tf.t9i32) * std::exp(((-static_cast<double>(149.230f)) * tf.t9i)));
    drevdt = (rev * ((((-1.5e0) * tf.t9i) + (static_cast<double>(149.230f) * tf.t9i2))));
    rr = (((den * den) * rev) * term);
    drrdt = (((den * den) * (((drevdt * term) + (rev * dtermdt)))) * 1.0e-9);
    drrdd = (((2.0e0 * den) * rev) * term);
    return {fr, rr};
}

// public_aprox21.f90:19266
static inline RatePair rate_he3he4(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dt9a = 0.0, dt9a13 = 0.0, dt9a56 = 0.0, dtermdt = 0.0, rev = 0.0, t9a = 0.0, t9a13 = 0.0, t9a56 = 0.0, term = 0.0, zz = 0.0;
    aa = (1.0e0 + (static_cast<double>(0.0495f) * tf.t9));
    daa = static_cast<double>(0.0495f);
    zz = (1.0e0 / aa);
    t9a = (tf.t9 * zz);
    dt9a = (((1.0e0 - (t9a * daa))) * zz);
    zz = (dt9a / t9a);
    t9a13 = std::pow(t9a, oneth);
    dt9a13 = ((oneth * t9a13) * zz);
    t9a56 = std::pow(t9a, fivsix);
    dt9a56 = ((fivsix * t9a56) * zz);
    term = (((5.61e+6 * t9a56) * tf.t9i32) * std::exp(((-static_cast<double>(12.826f)) / t9a13)));
    dtermdt = (term * ((((dt9a56 / t9a56) - (1.5e0 * tf.t9i)) + ((static_cast<double>(12.826f) / std::pow(t9a13, 2)) * dt9a13))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((static_cast<double>(1.11e+10f) * tf.t932) * std::exp(((-static_cast<double>(18.423f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(18.423f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:19523
static inline RatePair rate_c12pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, dd = 0.0, ddd = 0.0, dee = 0.0, drevdt = 0.0, dtermdt = 0.0, ee = 0.0, rev = 0.0, term = 0.0;
    const double q1 = (1.0e0 / 2.25e0);
    aa = ((static_cast<double>(2.04e+07f) * tf.t9i23) * std::exp((((-static_cast<double>(13.69f)) * tf.t9i13) - (tf.t92 * q1))));
    daa = (aa * (((((-twoth) * tf.t9i) + ((oneth * static_cast<double>(13.69f)) * tf.t9i43)) - ((2.0e0 * tf.t9) * q1))));
    bb = (((((1.0e0 + (static_cast<double>(0.03f) * tf.t913)) + (static_cast<double>(1.19f) * tf.t923)) + (static_cast<double>(0.254f) * tf.t9)) + (static_cast<double>(2.06f) * tf.t943)) + (static_cast<double>(1.12f) * tf.t953));
    dbb = ((((((oneth * static_cast<double>(0.03f)) * tf.t9i23) + ((twoth * static_cast<double>(1.19f)) * tf.t9i13)) + static_cast<double>(0.254f)) + ((fourth * static_cast<double>(2.06f)) * tf.t913)) + ((fiveth * static_cast<double>(1.12f)) * tf.t923));
    cc = (aa * bb);
    dcc = ((daa * bb) + (aa * dbb));
    dd = ((static_cast<double>(1.08e+05f) * tf.t9i32) * std::exp(((-static_cast<double>(4.925f)) * tf.t9i)));
    ddd = (dd * ((((-1.5e0) * tf.t9i) + (static_cast<double>(4.925f) * tf.t9i2))));
    ee = ((static_cast<double>(2.15e+05f) * tf.t9i32) * std::exp(((-static_cast<double>(18.179f)) * tf.t9i)));
    dee = (ee * ((((-1.5e0) * tf.t9i) + (static_cast<double>(18.179f) * tf.t9i2))));
    term = ((cc + dd) + ee);
    dtermdt = ((dcc + ddd) + dee);
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((static_cast<double>(8.84e+09f) * tf.t932) * std::exp(((-static_cast<double>(22.553f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(22.553f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:19656
static inline RatePair rate_n14pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, dd = 0.0, ddd = 0.0, dee = 0.0, drevdt = 0.0, dtermdt = 0.0, ee = 0.0, rev = 0.0, term = 0.0;
    const double q1 = (1.0e0 / 10.850436e0);
    aa = ((static_cast<double>(4.90e+07f) * tf.t9i23) * std::exp((((-static_cast<double>(15.228f)) * tf.t9i13) - (tf.t92 * q1))));
    daa = (aa * (((((-twoth) * tf.t9i) + ((oneth * static_cast<double>(15.228f)) * tf.t9i43)) - ((2.0e0 * tf.t9) * q1))));
    bb = (((((1.0e0 + (static_cast<double>(0.027f) * tf.t913)) - (static_cast<double>(0.778f) * tf.t923)) - (static_cast<double>(0.149f) * tf.t9)) + (static_cast<double>(0.261f) * tf.t943)) + (static_cast<double>(0.127f) * tf.t953));
    dbb = ((((((oneth * static_cast<double>(0.027f)) * tf.t9i23) - ((twoth * static_cast<double>(0.778f)) * tf.t9i13)) - static_cast<double>(0.149f)) + ((fourth * static_cast<double>(0.261f)) * tf.t913)) + ((fiveth * static_cast<double>(0.127f)) * tf.t923));
    cc = (aa * bb);
    dcc = ((daa * bb) + (aa * dbb));
    dd = ((static_cast<double>(2.37e+03f) * tf.t9i32) * std::exp(((-static_cast<double>(3.011f)) * tf.t9i)));
    ddd = (dd * ((((-1.5e0) * tf.t9i) + (static_cast<double>(3.011f) * tf.t9i2))));
    ee = (static_cast<double>(2.19e+04f) * std::exp(((-static_cast<double>(12.530f)) * tf.t9i)));
    dee = ((ee * static_cast<double>(12.530f)) * tf.t9i2);
    term = ((cc + dd) + ee);
    dtermdt = ((dcc + ddd) + dee);
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((static_cast<double>(2.70e+10f) * tf.t932) * std::exp(((-static_cast<double>(84.678f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(84.678f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:19793
static inline RatePair rate_n15pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, dd = 0.0, ddd = 0.0, dee = 0.0, dff = 0.0, drevdt = 0.0, dtermdt = 0.0, ee = 0.0, ff = 0.0, rev = 0.0, term = 0.0;
    const double q1 = (1.0e0 / 0.2025e0);
    aa = ((static_cast<double>(9.78e+08f) * tf.t9i23) * std::exp((((-static_cast<double>(15.251f)) * tf.t9i13) - (tf.t92 * q1))));
    daa = (aa * (((((-twoth) * tf.t9i) + ((oneth * static_cast<double>(15.251f)) * tf.t9i43)) - ((2.0e0 * tf.t9) * q1))));
    bb = (((((1.0e0 + (static_cast<double>(0.027f) * tf.t913)) + (static_cast<double>(0.219f) * tf.t923)) + (static_cast<double>(0.042f) * tf.t9)) + (static_cast<double>(6.83f) * tf.t943)) + (static_cast<double>(3.32f) * tf.t953));
    dbb = ((((((oneth * static_cast<double>(0.027f)) * tf.t9i23) + ((twoth * static_cast<double>(0.219f)) * tf.t9i13)) + static_cast<double>(0.042f)) + ((fourth * static_cast<double>(6.83f)) * tf.t913)) + ((fiveth * static_cast<double>(3.32f)) * tf.t923));
    cc = (aa * bb);
    dcc = ((daa * bb) + (aa * dbb));
    dd = ((static_cast<double>(1.11e+04f) * tf.t9i32) * std::exp(((-static_cast<double>(3.328f)) * tf.t9i)));
    ddd = (dd * ((((-1.5e0) * tf.t9i) + (static_cast<double>(3.328f) * tf.t9i2))));
    ee = ((static_cast<double>(1.49e+04f) * tf.t9i32) * std::exp(((-static_cast<double>(4.665f)) * tf.t9i)));
    dee = (ee * ((((-1.5e0) * tf.t9i) + (static_cast<double>(4.665f) * tf.t9i2))));
    ff = ((static_cast<double>(3.8e+06f) * tf.t9i32) * std::exp(((-static_cast<double>(11.048f)) * tf.t9i)));
    dff = (ff * ((((-1.5e0) * tf.t9i) + (static_cast<double>(11.048f) * tf.t9i2))));
    term = (((cc + dd) + ee) + ff);
    dtermdt = (((dcc + ddd) + dee) + dff);
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((static_cast<double>(3.62e+10f) * tf.t932) * std::exp(((-static_cast<double>(140.734f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(140.734f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:19849
static inline RatePair rate_n15pa(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, dd = 0.0, ddd = 0.0, dee = 0.0, dff = 0.0, dgg = 0.0, drevdt = 0.0, dtermdt = 0.0, ee = 0.0, ff = 0.0, gg = 0.0, rev = 0.0, term = 0.0;
    const double theta = 0.1e0;
    const double q1 = (1.0e0 / 0.272484e0);
    aa = ((1.08e+12 * tf.t9i23) * std::exp((((-static_cast<double>(15.251f)) * tf.t9i13) - (tf.t92 * q1))));
    daa = (aa * (((((-twoth) * tf.t9i) + ((oneth * static_cast<double>(15.251f)) * tf.t9i43)) - ((2.0e0 * tf.t9) * q1))));
    bb = (((((1.0e0 + (static_cast<double>(0.027f) * tf.t913)) + (static_cast<double>(2.62f) * tf.t923)) + (static_cast<double>(0.501f) * tf.t9)) + (static_cast<double>(5.36f) * tf.t943)) + (static_cast<double>(2.60f) * tf.t953));
    dbb = ((((((oneth * static_cast<double>(0.027f)) * tf.t9i23) + ((twoth * static_cast<double>(2.62f)) * tf.t9i13)) + static_cast<double>(0.501f)) + ((fourth * static_cast<double>(5.36f)) * tf.t913)) + ((fiveth * static_cast<double>(2.60f)) * tf.t923));
    cc = (aa * bb);
    dcc = ((daa * bb) + (aa * dbb));
    dd = ((1.19e+08 * tf.t9i32) * std::exp(((-static_cast<double>(3.676f)) * tf.t9i)));
    ddd = (dd * ((((-1.5e0) * tf.t9i) + (static_cast<double>(3.676f) * tf.t9i2))));
    ee = ((5.41e+08 * tf.t9i12) * std::exp(((-static_cast<double>(8.926f)) * tf.t9i)));
    dee = (ee * ((((-0.5e0) * tf.t9i) + (static_cast<double>(8.926f) * tf.t9i2))));
    ff = (((theta * 4.72e+08) * tf.t9i32) * std::exp(((-static_cast<double>(7.721f)) * tf.t9i)));
    dff = (ff * ((((-1.5e0) * tf.t9i) + (static_cast<double>(7.721f) * tf.t9i2))));
    gg = (((theta * 2.20e+09) * tf.t9i32) * std::exp(((-static_cast<double>(11.418f)) * tf.t9i)));
    dgg = (gg * ((((-1.5e0) * tf.t9i) + (static_cast<double>(11.418f) * tf.t9i2))));
    term = ((((cc + dd) + ee) + ff) + gg);
    dtermdt = ((((dcc + ddd) + dee) + dff) + dgg);
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = (7.06e-01 * std::exp(((-static_cast<double>(57.625f)) * tf.t9i)));
    drevdt = ((rev * static_cast<double>(57.625f)) * tf.t9i2);
    rr = ((den * rev) * term);
    drrdt = ((den * (((drevdt * term) + (rev * dtermdt)))) * 1.0e-9);
    drrdd = (rev * term);
    return {fr, rr};
}

// public_aprox21.f90:19909
static inline RatePair rate_o16pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, dd = 0.0, ddd = 0.0, dee = 0.0, drevdt = 0.0, dtermdt = 0.0, ee = 0.0, rev = 0.0, term = 0.0, zz = 0.0;
    aa = std::exp(((-static_cast<double>(0.728f)) * tf.t923));
    daa = ((((-twoth) * aa) * static_cast<double>(0.728f)) * tf.t9i13);
    bb = (1.0e0 + (static_cast<double>(2.13f) * ((1.0e0 - aa))));
    dbb = ((-static_cast<double>(2.13f)) * daa);
    cc = (tf.t923 * bb);
    dcc = (((twoth * cc) * tf.t9i) + (tf.t923 * dbb));
    dd = std::exp(((-static_cast<double>(16.692f)) * tf.t9i13));
    ddd = (((oneth * dd) * static_cast<double>(16.692f)) * tf.t9i43);
    zz = (1.0e0 / cc);
    ee = (dd * zz);
    dee = (((ddd - (ee * dcc))) * zz);
    term = (1.50e+08 * ee);
    dtermdt = (1.50e+08 * dee);
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((static_cast<double>(3.03e+09f) * tf.t932) * std::exp(((-static_cast<double>(6.968f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(6.968f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:19737
static inline RatePair rate_n14ag(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, cc = 0.0, daa = 0.0, dbb = 0.0, dcc = 0.0, dd = 0.0, ddd = 0.0, dee = 0.0, dff = 0.0, drevdt = 0.0, dtermdt = 0.0, ee = 0.0, ff = 0.0, rev = 0.0, term = 0.0;
    const double q1 = (1.0e0 / 0.776161e0);
    aa = ((7.78e+09 * tf.t9i23) * std::exp((((-static_cast<double>(36.031f)) * tf.t9i13) - (tf.t92 * q1))));
    daa = (aa * (((((-twoth) * tf.t9i) + ((oneth * static_cast<double>(36.031f)) * tf.t9i43)) - ((2.0e0 * tf.t9) * q1))));
    bb = (((((1.0e0 + (static_cast<double>(0.012f) * tf.t913)) + (static_cast<double>(1.45f) * tf.t923)) + (static_cast<double>(0.117f) * tf.t9)) + (static_cast<double>(1.97f) * tf.t943)) + (static_cast<double>(0.406f) * tf.t953));
    dbb = ((((((oneth * static_cast<double>(0.012f)) * tf.t9i23) + ((twoth * static_cast<double>(1.45f)) * tf.t9i13)) + static_cast<double>(0.117f)) + ((fourth * static_cast<double>(1.97f)) * tf.t913)) + ((fiveth * static_cast<double>(0.406f)) * tf.t923));
    cc = (aa * bb);
    dcc = ((daa * bb) + (aa * dbb));
    dd = ((2.36e-10 * tf.t9i32) * std::exp(((-static_cast<double>(2.798f)) * tf.t9i)));
    ddd = (dd * ((((-1.5e0) * tf.t9i) + (static_cast<double>(2.798f) * tf.t9i2))));
    ee = ((static_cast<double>(2.03f) * tf.t9i32) * std::exp(((-static_cast<double>(5.054f)) * tf.t9i)));
    dee = (ee * ((((-1.5e0) * tf.t9i) + (static_cast<double>(5.054f) * tf.t9i2))));
    ff = ((1.15e+04 * tf.t9i23) * std::exp(((-static_cast<double>(12.310f)) * tf.t9i)));
    dff = (ff * ((((-twoth) * tf.t9i) + (static_cast<double>(12.310f) * tf.t9i2))));
    term = (((cc + dd) + ee) + ff);
    dtermdt = (((dcc + ddd) + dee) + dff);
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((static_cast<double>(5.42e+10f) * tf.t932) * std::exp(((-static_cast<double>(51.236f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(51.236f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:23170
static inline RatePair rate_fe52ng(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, tq2 = 0.0;
    tq2 = (tf.t9 - 0.348e0);
    term = (9.604e+05 * std::exp(((-static_cast<double>(0.0626f)) * tq2)));
    dtermdt = ((-term) * static_cast<double>(0.0626f));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((2.43e+09 * tf.t932) * std::exp(((-static_cast<double>(123.951f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(123.951f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:23209
static inline RatePair rate_fe53ng(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double drevdt = 0.0, dtermdt = 0.0, dtq10 = 0.0, rev = 0.0, term = 0.0, tq1 = 0.0, tq10 = 0.0, tq2 = 0.0;
    tq1 = (tf.t9 / static_cast<double>(0.348f));
    tq10 = std::pow(tq1, static_cast<double>(0.10f));
    dtq10 = ((0.1e0 * tq10) / ((static_cast<double>(0.348f) * tq1)));
    tq2 = (tf.t9 - 0.348e0);
    term = ((1.817e+06 * tq10) * std::exp(((-static_cast<double>(0.06319f)) * tq2)));
    dtermdt = (((term / tq10) * dtq10) - (term * static_cast<double>(0.06319f)));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((1.56e+11 * tf.t932) * std::exp(((-static_cast<double>(155.284f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(155.284f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:23302
static inline RatePair rate_fe54pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, daa = 0.0, drevdt = 0.0, dtermdt = 0.0, rev = 0.0, term = 0.0, z = 0.0, z2 = 0.0, z3 = 0.0;
    z = std::min(tf.t9, 10.0e0);
    z2 = (z * z);
    z3 = (z2 * z);
    aa = (((1.0e0 + (9.593e-2 * z)) - (3.445e-3 * z2)) + (8.594e-5 * z3));
    if ((z == static_cast<double>(10.0f))) {
        daa = 0.0e0;
    } else {
        daa = ((9.593e-2 - ((2.0e0 * 3.445e-3) * tf.t9)) + ((3.0e0 * 8.594e-5) * tf.t92));
    }
    term = ((4.51e+17 * tf.t9i23) * std::exp((((-static_cast<double>(38.483f)) * tf.t9i13) * aa)));
    dtermdt = (term * ((((-twoth) * tf.t9i) + ((static_cast<double>(38.483f) * tf.t9i13) * ((((oneth * tf.t9i) * aa) - daa))))));
    fr = (den * term);
    dfrdt = ((den * dtermdt) * 1.0e-9);
    dfrdd = term;
    rev = ((2.400e+09 * tf.t932) * std::exp(((-static_cast<double>(58.605f)) * tf.t9i)));
    drevdt = (rev * (((1.5e0 * tf.t9i) + (static_cast<double>(58.605f) * tf.t9i2))));
    rr = (rev * term);
    drrdt = ((((drevdt * term) + (rev * dtermdt))) * 1.0e-9);
    drrdd = 0.0e0;
    return {fr, rr};
}

// public_aprox21.f90:23251
static inline RatePair rate_fe54ng(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, daa = 0.0, dbb = 0.0, dtermdt = 0.0, term = 0.0;
    aa = ((((((2.307390e+01 - (7.931795e-02 * tf.t9i)) + (7.535681e+00 * tf.t9i13)) - (1.595025e+01 * tf.t913)) + (1.377715e+00 * tf.t9)) - (1.291479e-01 * tf.t953)) + (6.707473e+00 * std::log(tf.t9)));
    daa = ((((((7.931795e-02 * tf.t9i2) - ((oneth * 7.535681e+00) * tf.t9i43)) - ((oneth * 1.595025e+01) * tf.t9i23)) + 1.377715e+00) - ((fiveth * 1.291479e-01) * tf.t923)) + (6.707473e+00 * tf.t9i));
    if ((aa < static_cast<double>(200.0f))) {
        term = std::exp(aa);
        dtermdt = ((term * daa) * 1.0e-9);
    } else {
        term = std::exp(200.0e0);
        dtermdt = 0.0e0;
    }
    bb = ((4.800293e+09 * tf.t932) * std::exp(((-1.078986e+02) * tf.t9i)));
    dbb = (bb * (((1.5e0 * tf.t9i) + (1.078986e+02 * tf.t9i2))));
    rr = (term * bb);
    drrdt = ((dtermdt * bb) + ((term * dbb) * 1.0e-9));
    drrdd = 0.0e0;
    dfrdd = term;
    fr = (term * den);
    dfrdt = (dtermdt * den);
    return {fr, rr};
}

// public_aprox21.f90:23350
static inline RatePair rate_fe54ap(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, daa = 0.0, dbb = 0.0, dtermdt = 0.0, term = 0.0;
    aa = ((((((3.97474900e+01 - (6.06543100e+00 * tf.t9i)) + (1.63239600e+02 * tf.t9i13)) - (2.20457700e+02 * tf.t913)) + (8.63980400e+00 * tf.t9)) - (3.45841300e-01 * tf.t953)) + (1.31464200e+02 * std::log(tf.t9)));
    daa = ((((((6.06543100e+00 * tf.t9i2) - ((oneth * 1.63239600e+02) * tf.t9i43)) - ((oneth * 2.20457700e+02) * tf.t9i23)) + 8.63980400e+00) - ((fiveth * 3.45841300e-01) * tf.t923)) + (1.31464200e+02 * tf.t9i));
    if ((aa < static_cast<double>(200.0f))) {
        term = std::exp(aa);
        dtermdt = ((term * daa) * 1.0e-9);
    } else {
        term = std::exp(200.0e0);
        dtermdt = 0.0e0;
    }
    bb = (2.16896000e+00 * std::exp(((-2.05631700e+01) * tf.t9i)));
    dbb = ((bb * 2.05631700e+01) * tf.t9i2);
    drrdd = term;
    rr = (term * den);
    drrdt = (dtermdt * den);
    fr = (rr * bb);
    dfrdt = ((drrdt * bb) + ((rr * dbb) * 1.0e-9));
    dfrdd = (drrdd * bb);
    return {fr, rr};
}

// public_aprox21.f90:23405
static inline RatePair rate_fe55ng(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, daa = 0.0, dbb = 0.0, dtermdt = 0.0, term = 0.0;
    aa = ((((((1.954115e+01 - (6.834029e-02 * tf.t9i)) + (5.379859e+00 * tf.t9i13)) - (8.758150e+00 * tf.t913)) + (5.285107e-01 * tf.t9)) - (4.973739e-02 * tf.t953)) + (4.065564e+00 * std::log(tf.t9)));
    daa = ((((((6.834029e-02 * tf.t9i2) - ((oneth * 5.379859e+00) * tf.t9i43)) - ((oneth * 8.758150e+00) * tf.t9i23)) + 5.285107e-01) - ((fiveth * 4.973739e-02) * tf.t923)) + (4.065564e+00 * tf.t9i));
    if ((aa < static_cast<double>(200.0f))) {
        term = std::exp(aa);
        dtermdt = ((term * daa) * 1.0e-9);
    } else {
        term = std::exp(200.0e0);
        dtermdt = 0.0e0;
    }
    bb = ((7.684279e+10 * tf.t932) * std::exp(((-1.299472e+02) * tf.t9i)));
    dbb = (bb * (((1.5e0 * tf.t9i) + (1.299472e+02 * tf.t9i2))));
    rr = (term * bb);
    drrdt = ((dtermdt * bb) + ((term * dbb) * 1.0e-9));
    drrdd = 0.0e0;
    dfrdd = term;
    fr = (term * den);
    dfrdt = (dtermdt * den);
    return {fr, rr};
}

// public_aprox21.f90:23457
static inline RatePair rate_fe56pg(double temp, double den, const TfactorsData& tf)
{
    double fr = 0.0, rr = 0.0;
    double dfrdt = 0.0, dfrdd = 0.0, drrdt = 0.0, drrdd = 0.0;
    double aa = 0.0, bb = 0.0, daa = 0.0, dbb = 0.0, dtermdt = 0.0, term = 0.0;
    aa = ((((((1.755960e+02 - (7.018872e+00 * tf.t9i)) + (2.800131e+02 * tf.t9i13)) - (4.749343e+02 * tf.t913)) + (2.683860e+01 * tf.t9)) - (1.542324e+00 * tf.t953)) + (2.315911e+02 * std::log(tf.t9)));
    daa = ((((((7.018872e+00 * tf.t9i2) - ((oneth * 2.800131e+02) * tf.t9i43)) - ((oneth * 4.749343e+02) * tf.t9i23)) + 2.683860e+01) - ((fiveth * 1.542324e+00) * tf.t923)) + (2.315911e+02 * tf.t9i));
    if ((aa < static_cast<double>(200.0f))) {
        term = std::exp(aa);
        dtermdt = ((term * daa) * 1.0e-9);
    } else {
        term = std::exp(200.0e0);
        dtermdt = 0.0e0;
    }
    bb = ((2.402486e+09 * tf.t932) * std::exp(((-6.995192e+01) * tf.t9i)));
    dbb = (bb * (((1.5e0 * tf.t9i) + (6.995192e+01 * tf.t9i2))));
    rr = (term * bb);
    drrdt = ((dtermdt * bb) + ((term * dbb) * 1.0e-9));
    drrdd = 0.0e0;
    dfrdd = term;
    fr = (term * den);
    dfrdt = (dtermdt * den);
    return {fr, rr};
}

}; // struct Aprox21RateLibrary
} // namespace timmes
