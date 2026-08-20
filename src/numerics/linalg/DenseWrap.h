/*
 * @file DenseWrap.h
 * @brief Dense LU solver with partial row pivoting.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "../../data/GlobalDefs.h"

struct DenseMatrixData
{
    double data[BurnLimits::MAX_ODE_NEQ][BurnLimits::MAX_ODE_NEQ];

    // Preserve the one-based matrix interface used by generated network code.
    double &operator()(int i, int j)
    {
        return data[i - 1][j - 1];
    }

    const double &operator()(int i, int j) const
    {
        return data[i - 1][j - 1];
    }

    void set(int i, int j, double val)
    {
        data[i - 1][j - 1] = val;
    }

    void zero()
    {
        // Clear the fixed maximum extent because this storage type does not
        // carry ACTIVE_N. Compact-network callers may specialize this later.
        for (int i = 0; i < BurnLimits::MAX_ODE_NEQ; ++i)
#pragma omp simd
            for (int j = 0; j < BurnLimits::MAX_ODE_NEQ; ++j)
                data[i][j] = 0.0;
    }
};

// Dense LU factorization and triangular solves.
struct DenseLUSolver
{
    /**
     * @brief Solve Ax=b in place.
     * @tparam ACTIVE_N Active network dimension, including temperature.
     * @tparam MAX_N Allocated storage extent, BurnLimits::MAX_ODE_NEQ.
     * @param A Input matrix, overwritten by its combined LU factors.
     * @param b Right-hand side, overwritten by the solution x.
     * @return False when the matrix is numerically singular.
     */
    template <int ACTIVE_N, int MAX_N>
    static bool solve(DenseMatrixData &A, double b[MAX_N])
    {
        int p[ACTIVE_N]; // Stack-resident logical row permutation.
#pragma omp simd
        for (int i = 0; i < ACTIVE_N; ++i)
            p[i] = i;

        // LU factorization with partial pivoting down each active column.
        for (int i = 0; i < ACTIVE_N; ++i)
        {
            // Select the largest available pivot magnitude in this column.
            double max_val = 0.0;
            int pivot_row = i;
            for (int j = i; j < ACTIVE_N; ++j)
            {
                double val = std::abs(A.data[p[j]][i]);
                if (val > max_val)
                {
                    max_val = val;
                    pivot_row = j;
                }
            }

            if (max_val < 1e-20)
                return false; // 1e-20 is the singular-pivot threshold; adaptive callers reduce dt.

            // Swap logical row indices rather than moving matrix storage.
            std::swap(p[i], p[pivot_row]);

            // Eliminate entries below the pivot.
            double pivot_inv = 1.0 / A.data[p[i]][i];
            for (int j = i + 1; j < ACTIVE_N; ++j)
            {
                A.data[p[j]][i] *= pivot_inv; // Store the L multiplier below the diagonal.
#pragma omp simd
                for (int k = i + 1; k < ACTIVE_N; ++k)
                {
                    A.data[p[j]][k] -= A.data[p[j]][i] * A.data[p[i]][k]; // Update the U factor.
                }
            }
        }

        // Forward substitution: solve Ly=Pb.
        double y[ACTIVE_N]; // Compact stack workspace for the active dimension.
        for (int i = 0; i < ACTIVE_N; ++i)
        {
            y[i] = b[p[i]];
            for (int j = 0; j < i; ++j)
            {
                y[i] -= A.data[p[i]][j] * y[j];
            }
        }

        // Back substitution: solve Ux=y and overwrite b with x.
        for (int i = ACTIVE_N - 1; i >= 0; --i)
        {
            b[i] = y[i];
            for (int j = i + 1; j < ACTIVE_N; ++j)
            {
                b[i] -= A.data[p[i]][j] * b[j];
            }
            b[i] /= A.data[p[i]][i];
        }

        return true;
    }

    /**
     * @brief Factor the matrix in O(N^3) without solving a right-hand side.
     */
    template <int ACTIVE_N, int MAX_N>
    static bool factorize(DenseMatrixData &A, int p[MAX_N])
    {
#pragma omp simd
        for (int i = 0; i < ACTIVE_N; ++i)
            p[i] = i;

        for (int i = 0; i < ACTIVE_N; ++i)
        {
            double max_val = 0.0;
            int pivot_row = i;
            for (int j = i; j < ACTIVE_N; ++j)
            {
                double val = std::abs(A.data[p[j]][i]);
                if (val > max_val)
                {
                    max_val = val;
                    pivot_row = j;
                }
            }

            if (max_val < 1e-20) return false;

            std::swap(p[i], p[pivot_row]);

            double pivot_inv = 1.0 / A.data[p[i]][i];
            for (int j = i + 1; j < ACTIVE_N; ++j)
            {
                A.data[p[j]][i] *= pivot_inv;
#pragma omp simd
                for (int k = i + 1; k < ACTIVE_N; ++k)
                {
                    A.data[p[j]][k] -= A.data[p[j]][i] * A.data[p[i]][k];
                }
            }
        }
        return true;
    }

    /**
     * @brief Solve with existing LU factors in O(N^2).
     */
    template <int ACTIVE_N, int MAX_N>
    static void solve_with_factors(const DenseMatrixData &A, const int p[MAX_N], double b[MAX_N])
    {
        double y[ACTIVE_N];
        for (int i = 0; i < ACTIVE_N; ++i)
        {
            y[i] = b[p[i]];
#pragma omp simd
            for (int j = 0; j < i; ++j)
            {
                y[i] -= A.data[p[i]][j] * y[j];
            }
        }

        for (int i = ACTIVE_N - 1; i >= 0; --i)
        {
            b[i] = y[i];
#pragma omp simd
            for (int j = i + 1; j < ACTIVE_N; ++j)
            {
                b[i] -= A.data[p[i]][j] * b[j];
            }
            b[i] /= A.data[p[i]][i];
        }
    }
};
