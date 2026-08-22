/**
 * @file SparseWrap.h
 * @brief Sparse CSC matrix and SuiteSparse KLU linear-solver policy.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#if ARCH_HAS_KLU
#include <klu.h>
#endif

template <int N>
struct SparseMatrixData
{
    std::vector<int> rows;
    std::vector<int> columns;
    std::vector<double> values;
    std::vector<int> slots;

    std::vector<int> column_pointers;
    std::vector<int> row_indices;
    std::vector<double> csc_values;
    std::vector<int> csc_to_slot;
    bool pattern_dirty = true;

#if ARCH_HAS_KLU
    klu_common common{};
    klu_symbolic *symbolic = nullptr;
    klu_numeric *numeric = nullptr;
    bool common_initialized = false;
#endif

    SparseMatrixData() : slots(static_cast<std::size_t>(N) * N, -1) {}
    SparseMatrixData(const SparseMatrixData &) = delete;
    SparseMatrixData &operator=(const SparseMatrixData &) = delete;

    ~SparseMatrixData()
    {
#if ARCH_HAS_KLU
        if (numeric != nullptr) klu_free_numeric(&numeric, &common);
        if (symbolic != nullptr) klu_free_symbolic(&symbolic, &common);
#endif
    }

    /**
     * Preserve the symbolic pattern between Jacobian evaluations. Generated
     * networks write the same structural entries at every state, so this lets
     * KLU reuse its symbolic analysis while only numeric values are refreshed.
     */
    void zero() { std::fill(values.begin(), values.end(), 0.0); }

    double operator()(int i, int j) const
    {
        validate(i, j);
        const int slot = slots[flat(i - 1, j - 1)];
        return slot < 0 ? 0.0 : values[slot];
    }

    void set(int i, int j, double value)
    {
        validate(i, j);
        const int row = i - 1;
        const int column = j - 1;
        int &slot = slots[flat(row, column)];
        if (slot < 0) {
            slot = static_cast<int>(values.size());
            rows.push_back(row);
            columns.push_back(column);
            values.push_back(value);
            pattern_dirty = true;
        } else {
            values[slot] = value;
        }
    }

    void form_shifted_identity(double scale)
    {
        for (double &value : values) value *= scale;
        for (int i = 1; i <= N; ++i) set(i, i, (*this)(i, i) + 1.0);
    }

    void set_shifted_identity_from(const SparseMatrixData &jacobian, double scale)
    {
        zero();
        for (std::size_t slot = 0; slot < jacobian.values.size(); ++slot) {
            set(jacobian.rows[slot] + 1, jacobian.columns[slot] + 1,
                scale * jacobian.values[slot]);
        }
        for (int i = 1; i <= N; ++i) set(i, i, (*this)(i, i) + 1.0);
    }

    void prepare_csc()
    {
        if (pattern_dirty) {
#if ARCH_HAS_KLU
            if (numeric != nullptr) klu_free_numeric(&numeric, &common);
            if (symbolic != nullptr) klu_free_symbolic(&symbolic, &common);
#endif
            std::vector<int> order(values.size());
            for (std::size_t k = 0; k < order.size(); ++k) order[k] = static_cast<int>(k);
            std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
                if (columns[lhs] != columns[rhs]) return columns[lhs] < columns[rhs];
                return rows[lhs] < rows[rhs];
            });

            column_pointers.assign(N + 1, 0);
            row_indices.resize(order.size());
            csc_values.resize(order.size());
            csc_to_slot.resize(order.size());
            for (int slot : order) ++column_pointers[columns[slot] + 1];
            for (int column = 0; column < N; ++column) {
                column_pointers[column + 1] += column_pointers[column];
            }
            for (std::size_t k = 0; k < order.size(); ++k) {
                row_indices[k] = rows[order[k]];
                csc_to_slot[k] = order[k];
            }
            pattern_dirty = false;
        }
        for (std::size_t k = 0; k < csc_values.size(); ++k) {
            csc_values[k] = values[csc_to_slot[k]];
        }
    }

private:
    static constexpr std::size_t flat(int row, int column)
    {
        return static_cast<std::size_t>(column) * N + row;
    }

    static void validate(int i, int j)
    {
        if (i < 1 || i > N || j < 1 || j > N) {
            throw std::out_of_range(
                "SparseMatrixData uses one-based indices inside its active extent");
        }
    }
};

struct SparseKLUSolver
{
    template <int ACTIVE_N, int MAX_N>
    static bool solve(SparseMatrixData<ACTIVE_N> &A, double b[MAX_N])
    {
        int unused[MAX_N]{};
        if (!factorize<ACTIVE_N, MAX_N>(A, unused)) return false;
        return solve_factored<ACTIVE_N>(A, b);
    }

    template <int ACTIVE_N, int MAX_N>
    static bool factorize(SparseMatrixData<ACTIVE_N> &A, int[MAX_N])
    {
#if ARCH_HAS_KLU
        A.prepare_csc();
        if (!A.common_initialized) {
            if (klu_defaults(&A.common) == 0) return false;
            A.common_initialized = true;
        }
        if (A.symbolic == nullptr) {
            A.symbolic = klu_analyze(ACTIVE_N, A.column_pointers.data(),
                                     A.row_indices.data(), &A.common);
            if (A.symbolic == nullptr) return false;
        }
        if (A.numeric != nullptr) {
            if (klu_refactor(A.column_pointers.data(), A.row_indices.data(),
                             A.csc_values.data(), A.symbolic, A.numeric,
                             &A.common) != 0) {
                return true;
            }
            klu_free_numeric(&A.numeric, &A.common);
        }
        A.numeric = klu_factor(A.column_pointers.data(), A.row_indices.data(),
                               A.csc_values.data(), A.symbolic, &A.common);
        return A.numeric != nullptr;
#else
        (void)A;
        throw std::runtime_error(
            "SparseKLU was selected, but ARCH was built without SuiteSparse KLU");
#endif
    }

    template <int ACTIVE_N, int MAX_N>
    static void solve_with_factors(const SparseMatrixData<ACTIVE_N> &matrix,
                                   const int[MAX_N], double b[MAX_N])
    {
        auto &A = const_cast<SparseMatrixData<ACTIVE_N> &>(matrix);
        if (!solve_factored<ACTIVE_N>(A, b)) {
            std::fill(b, b + ACTIVE_N,
                      std::numeric_limits<double>::quiet_NaN());
        }
    }

private:
    template <int ACTIVE_N>
    static bool solve_factored(SparseMatrixData<ACTIVE_N> &A, double *b)
    {
#if ARCH_HAS_KLU
        return A.symbolic != nullptr && A.numeric != nullptr &&
               klu_solve(A.symbolic, A.numeric, ACTIVE_N, 1, b, &A.common) != 0;
#else
        (void)A;
        (void)b;
        throw std::runtime_error(
            "SparseKLU was selected, but ARCH was built without SuiteSparse KLU");
#endif
    }
};
