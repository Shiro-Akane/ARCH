// ARCH-owned dual-number utility used to differentiate Timmes-derived network
// expressions; no upstream Timmes source is transcribed in this file.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#ifndef TIMMES_HD
#  if defined(__CUDACC__)
#    define TIMMES_HD __host__ __device__
#  else
#    define TIMMES_HD
#  endif
#endif

namespace timmes {

template <std::size_t N>
struct Dual {
    double value = 0.0;
    std::array<double, N> deriv{};

    TIMMES_HD Dual() = default;
    TIMMES_HD Dual(double v) : value(v) {}

    TIMMES_HD static Dual variable(double v, std::size_t index)
    {
        Dual result(v);
        result.deriv[index] = 1.0;
        return result;
    }

    TIMMES_HD Dual& operator+=(const Dual& rhs)
    {
        value += rhs.value;
#pragma omp simd
        for (std::size_t i = 0; i < N; ++i) deriv[i] += rhs.deriv[i];
        return *this;
    }
    TIMMES_HD Dual& operator-=(const Dual& rhs)
    {
        value -= rhs.value;
#pragma omp simd
        for (std::size_t i = 0; i < N; ++i) deriv[i] -= rhs.deriv[i];
        return *this;
    }
    TIMMES_HD Dual& operator*=(const Dual& rhs)
    {
        const double lhs_value = value;
        value *= rhs.value;
#pragma omp simd
        for (std::size_t i = 0; i < N; ++i) {
            deriv[i] = deriv[i] * rhs.value + lhs_value * rhs.deriv[i];
        }
        return *this;
    }
    TIMMES_HD Dual& operator/=(const Dual& rhs)
    {
        const double lhs_value = value;
        const double inv = 1.0 / rhs.value;
        value *= inv;
#pragma omp simd
        for (std::size_t i = 0; i < N; ++i) {
            deriv[i] = (deriv[i] - value * rhs.deriv[i]) * inv;
        }
        (void)lhs_value;
        return *this;
    }
};

template <std::size_t N> TIMMES_HD inline Dual<N> operator+(Dual<N> a, const Dual<N>& b) { return a += b; }
template <std::size_t N> TIMMES_HD inline Dual<N> operator-(Dual<N> a, const Dual<N>& b) { return a -= b; }
template <std::size_t N> TIMMES_HD inline Dual<N> operator*(Dual<N> a, const Dual<N>& b) { return a *= b; }
template <std::size_t N> TIMMES_HD inline Dual<N> operator/(Dual<N> a, const Dual<N>& b) { return a /= b; }
template <std::size_t N> TIMMES_HD inline Dual<N> operator+(Dual<N> a, double b) { return a += Dual<N>(b); }
template <std::size_t N> TIMMES_HD inline Dual<N> operator+(double a, Dual<N> b) { return b += Dual<N>(a); }
template <std::size_t N> TIMMES_HD inline Dual<N> operator-(Dual<N> a, double b) { return a -= Dual<N>(b); }
template <std::size_t N> TIMMES_HD inline Dual<N> operator-(double a, const Dual<N>& b) { return Dual<N>(a) -= b; }
template <std::size_t N> TIMMES_HD inline Dual<N> operator*(Dual<N> a, double b) { return a *= Dual<N>(b); }
template <std::size_t N> TIMMES_HD inline Dual<N> operator*(double a, Dual<N> b) { return b *= Dual<N>(a); }
template <std::size_t N> TIMMES_HD inline Dual<N> operator/(Dual<N> a, double b) { return a /= Dual<N>(b); }
template <std::size_t N> TIMMES_HD inline Dual<N> operator/(double a, const Dual<N>& b) { return Dual<N>(a) /= b; }
template <std::size_t N> TIMMES_HD inline Dual<N> operator-(Dual<N> a) {
    a.value = -a.value;
#pragma omp simd
    for (std::size_t i = 0; i < N; ++i) a.deriv[i] = -a.deriv[i];
    return a;
}

template <std::size_t N> TIMMES_HD inline bool operator<(const Dual<N>& a, double b) { return a.value < b; }
template <std::size_t N> TIMMES_HD inline bool operator>(const Dual<N>& a, double b) { return a.value > b; }
template <std::size_t N> TIMMES_HD inline bool operator<=(const Dual<N>& a, double b) { return a.value <= b; }
template <std::size_t N> TIMMES_HD inline bool operator>=(const Dual<N>& a, double b) { return a.value >= b; }

TIMMES_HD inline double value_of(double x) { return x; }
template <std::size_t N> TIMMES_HD inline double value_of(const Dual<N>& x) { return x.value; }

TIMMES_HD inline double exp_value(double x) { return std::exp(x); }
template <std::size_t N> TIMMES_HD inline Dual<N> exp_value(const Dual<N>& x)
{
    Dual<N> result(std::exp(x.value));
#pragma omp simd
    for (std::size_t i = 0; i < N; ++i) result.deriv[i] = result.value * x.deriv[i];
    return result;
}

TIMMES_HD inline double log_value(double x) { return std::log(x); }
template <std::size_t N> TIMMES_HD inline Dual<N> log_value(const Dual<N>& x)
{
    Dual<N> result(std::log(x.value));
    const double inv = 1.0 / x.value;
#pragma omp simd
    for (std::size_t i = 0; i < N; ++i) result.deriv[i] = x.deriv[i] * inv;
    return result;
}

TIMMES_HD inline double sqrt_value(double x) { return std::sqrt(x); }
template <std::size_t N> TIMMES_HD inline Dual<N> sqrt_value(const Dual<N>& x)
{
    Dual<N> result(std::sqrt(x.value));
    const double scale = 0.5 / result.value;
#pragma omp simd
    for (std::size_t i = 0; i < N; ++i) result.deriv[i] = x.deriv[i] * scale;
    return result;
}

TIMMES_HD inline double pow_value(double x, double exponent) { return std::pow(x, exponent); }
template <std::size_t N> TIMMES_HD inline Dual<N> pow_value(const Dual<N>& x, double exponent)
{
    Dual<N> result(std::pow(x.value, exponent));
    const double scale = exponent * std::pow(x.value, exponent - 1.0);
#pragma omp simd
    for (std::size_t i = 0; i < N; ++i) result.deriv[i] = scale * x.deriv[i];
    return result;
}

template <typename Scalar>
TIMMES_HD inline Scalar clamp_by_value(const Scalar& x, double lower, double upper)
{
    if (value_of(x) < lower) return Scalar(lower);
    if (value_of(x) > upper) return Scalar(upper);
    return x;
}

template <typename Scalar>
TIMMES_HD inline Scalar min_by_value(const Scalar& x, double upper)
{
    return value_of(x) < upper ? x : Scalar(upper);
}

} // namespace timmes
