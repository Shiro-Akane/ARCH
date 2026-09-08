/**
 * @file CsrMatrixView.h
 * @brief Backend-neutral, non-owning sparse matrix values over a fixed pattern.
 *
 * Matrix policies use one-based indices; CSR storage uses zero-based indices.
 * The pattern includes structural zeros and every diagonal. Its owner is host
 * metadata, whereas values and this view may reside on either execution backend.
 * No allocation, factorization handle, or nuclear-network formula belongs here.
 */
#pragma once

#include "../../core/ArchPortability.h"
#include <cmath>
#include <limits>

template <int N>
struct CsrMatrixView
{
    static_assert(N > 0);
    const int* row_offsets = nullptr;
    const int* column_indices = nullptr;
    double* values = nullptr;
    int nonzeros = 0;
    // A missing structural entry must reject the solve, not silently prune a
    // reaction. The latch remains false until the owner rebinds this view.
    bool pattern_valid = true;

    ARCH_HOST_DEVICE bool valid() const
    {
        return pattern_valid && row_offsets != nullptr
            && column_indices != nullptr && values != nullptr && nonzeros >= N;
    }

    ARCH_HOST_DEVICE int slot(int row, int column) const
    {
        if (row < 1 || row > N || column < 1 || column > N
            || row_offsets == nullptr || column_indices == nullptr)
            return -1;
        int begin = row_offsets[row - 1];
        int end = row_offsets[row];
        if (begin < 0 || end < begin || end > nonzeros) return -1;
        const int target = column - 1;
        while (begin < end) {
            const int middle = begin + (end - begin) / 2;
            if (column_indices[middle] < target) begin = middle + 1;
            else end = middle;
        }
        return begin < row_offsets[row] && column_indices[begin] == target
            ? begin : -1;
    }

    ARCH_HOST_DEVICE double operator()(int row, int column) const
    {
        const int index = slot(row, column);
        return index < 0 || values == nullptr ? 0.0 : values[index];
    }

    ARCH_HOST_DEVICE void set(int row, int column, double value)
    {
        const int index = slot(row, column);
        if (index < 0 || values == nullptr) {
            pattern_valid = false;
            return;
        }
        values[index] = value;
    }

    ARCH_HOST_DEVICE void zero()
    {
        if (!valid()) return;
        for (int index = 0; index < nonzeros; ++index) values[index] = 0.0;
    }

    ARCH_HOST_DEVICE void form_shifted_identity(double scale)
    {
        if (!valid()) return;
        for (int index = 0; index < nonzeros; ++index) values[index] *= scale;
        for (int row = 1; row <= N; ++row)
            set(row, row, (*this)(row, row) + 1.0);
    }

    ARCH_HOST_DEVICE void set_shifted_identity_from(
        const CsrMatrixView& jacobian, double scale)
    {
        if (!jacobian.valid()) {
            pattern_valid = false;
            return;
        }
        zero();
        // Do not assume equal allocations or transpose CSC values accidentally.
        for (int row = 0; row < N; ++row) {
            for (int index = jacobian.row_offsets[row];
                 index < jacobian.row_offsets[row + 1]; ++index)
                set(row + 1, jacobian.column_indices[index] + 1,
                    scale * jacobian.values[index]);
        }
        for (int row = 1; row <= N; ++row)
            set(row, row, (*this)(row, row) + 1.0);
    }

    /** Componentwise backward-error check over the ORIGINAL matrix and RHS.
     * A provider can report successful factorization after perturbing a tiny
     * pivot; this common numerical gate prevents accepting an inaccurate solve.
     * The O(N*epsilon) allowance covers sparse dot-product roundoff, not the
     * physical ODE tolerance. No matrix or state must return to the CPU for it.
     */
    ARCH_HOST_DEVICE bool solution_accurate(const double* rhs, const double* solution) const
    {
        if (!valid() || rhs == nullptr || solution == nullptr) return false;
        constexpr double allowance = 64.0 * (N + 1.0) * std::numeric_limits<double>::epsilon();
        for (int row = 0; row < N; ++row) {
            if (!std::isfinite(rhs[row])) return false;
            double product = 0.0;
            double scale = std::abs(rhs[row]);
            for (int index = row_offsets[row]; index < row_offsets[row + 1]; ++index) {
                const double x = solution[column_indices[index]];
                const double a = values[index];
                if (!std::isfinite(x) || !std::isfinite(a)) return false;
                const double term = a * x;
                product += term;
                scale += std::abs(term);
            }
            if (!std::isfinite(product) || !std::isfinite(scale)) return false;
            const double residual = std::abs(product - rhs[row]);
            if (residual > allowance * scale) return false;
        }
        return true;
    }
};
