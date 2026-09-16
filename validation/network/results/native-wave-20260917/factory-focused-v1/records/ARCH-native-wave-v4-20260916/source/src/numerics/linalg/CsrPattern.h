/**
 * @file CsrPattern.h
 * @brief Host-only construction of immutable, shared sparse structure.
 */
#pragma once

#include "CsrMatrixView.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace arch::linalg
{
struct CsrPattern
{
    int extent = 0;
    std::vector<int> row_offsets;
    std::vector<int> column_indices;

    template <int N>
    CsrMatrixView<N> view(double* values) const
    {
        if (extent != N || values == nullptr)
            throw std::invalid_argument("CSR value view has the wrong extent or null storage");
        return {row_offsets.data(), column_indices.data(), values,
                static_cast<int>(column_indices.size()), true};
    }
};

/**
 * A network's structural Jacobian writer can target this duck-typed sink.
 * Every set(), including set(..., 0.0), records structure. An evaluated zero is
 * not evidence that a reaction derivative is structurally absent. A generated
 * network should preferably enumerate its symbolic writes without rate/EOS work.
 */
class CsrPatternBuilder
{
public:
    explicit CsrPatternBuilder(int extent) : extent_(extent)
    {
        if (extent <= 0) throw std::invalid_argument("CSR extent must be positive");
    }

    void zero() {} // Numeric clearing never destroys symbolic structure.

    void set(int row, int column, double)
    {
        if (row < 1 || row > extent_ || column < 1 || column > extent_)
            throw std::out_of_range("CSR pattern uses one-based active indices");
        entries_.emplace_back(row - 1, column - 1);
    }

    // Explicit one-based index: a final row need not be a temperature.
    void include_dense_row_column(int index)
    {
        for (int row = 1; row <= extent_; ++row) {
            set(row, index, 0.0);
            set(index, row, 0.0);
        }
    }

    CsrPattern finish()
    {
        for (int row = 1; row <= extent_; ++row) set(row, row, 0.0);
        std::sort(entries_.begin(), entries_.end());
        entries_.erase(std::unique(entries_.begin(), entries_.end()), entries_.end());
        if (entries_.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::overflow_error("CSR pattern exceeds the 32-bit sparse index contract");
        CsrPattern result;
        result.extent = extent_;
        result.row_offsets.assign(static_cast<std::size_t>(extent_) + 1, 0);
        result.column_indices.reserve(entries_.size());
        for (const auto& entry : entries_) {
            ++result.row_offsets[entry.first + 1];
            result.column_indices.push_back(entry.second);
        }
        for (int row = 0; row < extent_; ++row)
            result.row_offsets[row + 1] += result.row_offsets[row];
        return result;
    }

private:
    int extent_;
    std::vector<std::pair<int, int>> entries_;
};
} // namespace arch::linalg
