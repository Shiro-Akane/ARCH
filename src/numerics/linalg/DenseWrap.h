/*
 * @file DenseWrap.h
 * @brief Fixed-size dense LU solver for compact reaction networks.
 */
#pragma once

#include <algorithm>
#include <cmath>

#include "../../core/ArchPortability.h"

/**
 * Dense storage is deliberately limited by dispatch to networks with at most
 * BurnLimits::MAX_SPECIES nuclei. N remains a template parameter so a compact
 * network only clears and factors its active extent.
 */
template <int N>
struct DenseMatrixData
{
    double data[N][N]{};

    // Preserve the one-based matrix interface used by generated network code.
    ARCH_HOST_DEVICE double &operator()(int i, int j)
    {
        return data[i - 1][j - 1];
    }

    ARCH_HOST_DEVICE const double &operator()(int i, int j) const
    {
        return data[i - 1][j - 1];
    }

    ARCH_HOST_DEVICE void set(int i, int j, double val)
    {
        data[i - 1][j - 1] = val;
    }

    ARCH_HOST_DEVICE void zero()
    {
        for (int i = 0; i < N; ++i)
#pragma omp simd
            for (int j = 0; j < N; ++j) data[i][j] = 0.0;
    }

    /// Replace J by I + scale*J without imposing a dense operation on sparse backends.
    ARCH_HOST_DEVICE void form_shifted_identity(double scale)
    {
        for (int i = 0; i < N; ++i) {
#pragma omp simd
            for (int j = 0; j < N; ++j) data[i][j] *= scale;
            data[i][i] += 1.0;
        }
    }

    ARCH_HOST_DEVICE void set_shifted_identity_from(
        const DenseMatrixData &jacobian, double scale)
    {
        for (int i = 0; i < N; ++i) {
#pragma omp simd
            for (int j = 0; j < N; ++j) data[i][j] = scale * jacobian.data[i][j];
            data[i][i] += 1.0;
        }
    }
};

struct DenseLUSolver
{
    template <int ACTIVE_N, int MAX_N, int STORAGE_N>
    ARCH_HOST_DEVICE static bool solve(
        DenseMatrixData<STORAGE_N> &A, double b[MAX_N])
    {
        static_assert(STORAGE_N >= ACTIVE_N);
        int pivots[MAX_N]{};
        if (!factorize<ACTIVE_N, MAX_N>(A, pivots)) return false;
        solve_with_factors<ACTIVE_N, MAX_N>(A, pivots, b);
        return true;
    }

    template <int ACTIVE_N, int MAX_N, int STORAGE_N>
    ARCH_HOST_DEVICE static bool factorize(
        DenseMatrixData<STORAGE_N> &A, int p[MAX_N])
    {
        static_assert(STORAGE_N >= ACTIVE_N);
#pragma omp simd
        for (int i = 0; i < ACTIVE_N; ++i) p[i] = i;

        for (int i = 0; i < ACTIVE_N; ++i) {
            double max_value = 0.0;
            int pivot_row = i;
            for (int j = i; j < ACTIVE_N; ++j) {
                const double value = std::abs(A.data[p[j]][i]);
                if (value > max_value) {
                    max_value = value;
                    pivot_row = j;
                }
            }
            if (max_value < 1.0e-20) return false;
            std::swap(p[i], p[pivot_row]);

            const double pivot_inverse = 1.0 / A.data[p[i]][i];
            for (int j = i + 1; j < ACTIVE_N; ++j) {
                A.data[p[j]][i] *= pivot_inverse;
#pragma omp simd
                for (int k = i + 1; k < ACTIVE_N; ++k) {
                    A.data[p[j]][k] -= A.data[p[j]][i] * A.data[p[i]][k];
                }
            }
        }
        return true;
    }

    template <int ACTIVE_N, int MAX_N, int STORAGE_N>
    ARCH_HOST_DEVICE static void solve_with_factors(
        const DenseMatrixData<STORAGE_N> &A,
        const int p[MAX_N], double b[MAX_N])
    {
        static_assert(STORAGE_N >= ACTIVE_N);
        double y[ACTIVE_N];
        for (int i = 0; i < ACTIVE_N; ++i) {
            y[i] = b[p[i]];
            for (int j = 0; j < i; ++j) y[i] -= A.data[p[i]][j] * y[j];
        }
        for (int i = ACTIVE_N - 1; i >= 0; --i) {
            b[i] = y[i];
            for (int j = i + 1; j < ACTIVE_N; ++j) b[i] -= A.data[p[i]][j] * b[j];
            b[i] /= A.data[p[i]][i];
        }
    }
};
