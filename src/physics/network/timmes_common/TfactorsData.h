#pragma once

#include <cmath>

namespace timmes {

inline constexpr double oneth = 1.0 / 3.0;
inline constexpr double twoth = 2.0 / 3.0;
inline constexpr double fourth = 4.0 / 3.0;
inline constexpr double fiveth = 5.0 / 3.0;
inline constexpr double elvnth = 11.0 / 3.0;
inline constexpr double fivfour = 1.25;
inline constexpr double onesix = 1.0 / 6.0;
inline constexpr double fivsix = 5.0 / 6.0;
inline constexpr double sevsix = 7.0 / 6.0;
inline constexpr double onefif = 0.2;
inline constexpr double sixfif = 1.2;
inline constexpr double onesev = 1.0 / 7.0;
inline constexpr double twosev = 2.0 / 7.0;
inline constexpr double foursev = 4.0 / 7.0;

// Direct translation of tfactors.dek and public_aprox21.f90:13207.
struct TfactorsData {
    double t9, t92, t93, t94, t95, t96;
    double t912, t932, t952, t972;
    double t913, t923, t943, t953, t973, t9113;
    double t914, t934, t954, t974;
    double t915, t935, t945, t965;
    double t916, t976, t9i76;
    double t917, t927, t947;
    double t918, t938, t958;
    double t9i, t9i2, t9i3;
    double t9i12, t9i32, t9i52, t9i72;
    double t9i13, t9i23, t9i43, t9i53;
    double t9i14, t9i34, t9i54;
    double t9i15, t9i35, t9i45, t9i65;
    double t9i17, t9i27, t9i47;
    double t9i18, t9i38, t9i58;
};

TIMMES_HD inline TfactorsData compute_tfactors(double temperature)
{
    TfactorsData f{};
    f.t9 = temperature * 1.0e-9;
    f.t92 = f.t9 * f.t9;
    f.t93 = f.t9 * f.t92;
    f.t94 = f.t9 * f.t93;
    f.t95 = f.t9 * f.t94;
    f.t96 = f.t9 * f.t95;

    f.t912 = std::sqrt(f.t9);
    f.t932 = f.t9 * f.t912;
    f.t952 = f.t9 * f.t932;
    f.t972 = f.t9 * f.t952;

    f.t913 = std::pow(f.t9, oneth);
    f.t923 = f.t913 * f.t913;
    f.t943 = f.t9 * f.t913;
    f.t953 = f.t9 * f.t923;
    f.t973 = f.t953 * f.t923;
    f.t9113 = f.t973 * f.t943;

    f.t914 = std::pow(f.t9, 0.25);
    f.t934 = f.t914 * f.t914 * f.t914;
    f.t954 = f.t9 * f.t914;
    f.t974 = f.t9 * f.t934;

    f.t915 = std::pow(f.t9, onefif);
    f.t935 = f.t915 * f.t915 * f.t915;
    f.t945 = f.t915 * f.t935;
    f.t965 = f.t9 * f.t915;

    f.t916 = std::pow(f.t9, onesix);
    f.t976 = f.t9 * f.t916;
    f.t9i76 = 1.0 / f.t976;

    f.t917 = std::pow(f.t9, onesev);
    f.t927 = f.t917 * f.t917;
    f.t947 = f.t927 * f.t927;

    f.t918 = std::sqrt(f.t914);
    f.t938 = f.t918 * f.t918 * f.t918;
    f.t958 = f.t938 * f.t918 * f.t918;

    f.t9i = 1.0 / f.t9;
    f.t9i2 = f.t9i * f.t9i;
    f.t9i3 = f.t9i2 * f.t9i;
    f.t9i12 = 1.0 / f.t912;
    f.t9i32 = f.t9i * f.t9i12;
    f.t9i52 = f.t9i * f.t9i32;
    f.t9i72 = f.t9i * f.t9i52;
    f.t9i13 = 1.0 / f.t913;
    f.t9i23 = f.t9i13 * f.t9i13;
    f.t9i43 = f.t9i * f.t9i13;
    f.t9i53 = f.t9i * f.t9i23;
    f.t9i14 = 1.0 / f.t914;
    f.t9i34 = f.t9i14 * f.t9i14 * f.t9i14;
    f.t9i54 = f.t9i * f.t9i14;
    f.t9i15 = 1.0 / f.t915;
    f.t9i35 = f.t9i15 * f.t9i15 * f.t9i15;
    f.t9i45 = f.t9i15 * f.t9i35;
    f.t9i65 = f.t9i * f.t9i15;
    f.t9i17 = 1.0 / f.t917;
    f.t9i27 = f.t9i17 * f.t9i17;
    f.t9i47 = f.t9i27 * f.t9i27;
    f.t9i18 = 1.0 / f.t918;
    f.t9i38 = f.t9i18 * f.t9i18 * f.t9i18;
    f.t9i58 = f.t9i38 * f.t9i18 * f.t9i18;
    return f;
}

} // namespace timmes
