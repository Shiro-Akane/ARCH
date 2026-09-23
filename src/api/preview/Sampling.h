/**
 * @file Sampling.h
 * @brief Select bounded samples from cells without exposing Driver storage.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Select bounded samples from cells without exposing Driver storage.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include <cstdint>
#include <stdexcept>

#include "api/Preview.h"

namespace arch::api {
class SamplingLimitError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};
struct SamplingPlan {
    int nx, ny;
    std::size_t count;
    bool two_dimensional;
};

// All products and sample budgets are checked before allocating field arrays.
/** Resolve 1D/2D sample counts under the protocol budget before allocation. */
inline SamplingPlan ResolveSampling(const PreviewRequest &request) {
    const bool two_d = request.case_id == "CellularDet";
    if (!two_d) {
        if (request.samples_x1 || request.samples_x2)
            throw std::invalid_argument("Axis sampling options are only supported for CellularDet 2D");
        if (request.sample_count < 2 || request.sample_count > max_sample_count)
            throw SamplingLimitError("Sod requires 2..4096 samples");
        return {request.sample_count, 1, std::size_t(request.sample_count), false};
    }
    if (request.sample_count_provided || request.sample_count != default_sample_count)
        throw std::invalid_argument("CellularDet 2D uses --samples-x1 and --samples-x2, not --samples");
    if (request.samples_x1.has_value() != request.samples_x2.has_value())
        throw std::invalid_argument("Provide both --samples-x1 and --samples-x2, or neither");
    const int nx = request.samples_x1.value_or(default_samples_2d);
    const int ny = request.samples_x2.value_or(default_samples_2d);
    if (nx < 2 || nx > max_samples_per_axis_2d || ny < 2 || ny > max_samples_per_axis_2d)
        throw SamplingLimitError("CellularDet requires 2..256 samples per axis");
    const auto count = std::uint64_t(nx) * std::uint64_t(ny);
    if (count > max_total_samples_2d)
        throw SamplingLimitError("CellularDet exceeds the total sample budget");
    return {nx, ny, std::size_t(count), true};
}
} // namespace arch::api
