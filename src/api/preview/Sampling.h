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
// Bounded 3D budgets, negotiated through the initialSampling extension.
inline constexpr int default_samples_3d = 32;
inline constexpr int max_samples_per_axis_3d = 64;
inline constexpr std::size_t max_total_samples_3d = 32768;
struct SamplingPlan {
    int nx, ny;
    std::size_t count;
    bool two_dimensional;
    int nz = 1;
};

// The caller supplies the parsed configuration dimension, not a case-name guess.
// Validate all products before allocating field arrays.
/** Resolve bounded extents for a one-, two- or three-axis request. */
inline SamplingPlan ResolveSampling(const PreviewRequest &request, int dimension) {
    if (dimension < 1 || dimension > 3)
        throw std::invalid_argument("Sampling dimension must be 1, 2 or 3");
    if (dimension == 1) {
        if (request.samples_x1 || request.samples_x2 || request.samples_x3)
            throw std::invalid_argument("Axis sampling options require a multidimensional preview");
        if (request.sample_count < 2 || request.sample_count > max_sample_count)
            throw SamplingLimitError("One-dimensional preview requires 2..4096 samples");
        return {request.sample_count, 1, std::size_t(request.sample_count), false, 1};
    }
    if (request.sample_count_provided || request.sample_count != default_sample_count)
        throw std::invalid_argument("Multidimensional preview uses axis samples, not --samples");
    if (dimension == 2 && request.samples_x3)
        throw std::invalid_argument("Two-dimensional preview does not accept third-axis samples");
    const bool any = request.samples_x1 || request.samples_x2 || request.samples_x3;
    const bool all = request.samples_x1 && request.samples_x2
        && (dimension == 2 || request.samples_x3);
    if (any && !all)
        throw std::invalid_argument("Provide all active-axis sample counts, or none");
    const int default_axis = dimension == 2 ? default_samples_2d : default_samples_3d;
    const int max_axis = dimension == 2 ? max_samples_per_axis_2d : max_samples_per_axis_3d;
    const std::size_t max_total = dimension == 2 ? max_total_samples_2d : max_total_samples_3d;
    const int nx = request.samples_x1.value_or(default_axis);
    const int ny = request.samples_x2.value_or(default_axis);
    const int nz = dimension == 3 ? request.samples_x3.value_or(default_axis) : 1;
    const int extents[] = {nx, ny, nz};
    for (int axis = 0; axis < dimension; ++axis)
        if (extents[axis] < 2 || extents[axis] > max_axis)
            throw SamplingLimitError("Axis samples exceed the dimensional working budget");
    const auto count = std::uint64_t(nx) * std::uint64_t(ny) * std::uint64_t(nz);
    if (count > max_total)
        throw SamplingLimitError("Preview exceeds the total sample budget");
    return {nx, ny, std::size_t(count), dimension == 2, nz};
}
/** Preserve existing callers until parsed-dimension generation integration. */
inline SamplingPlan ResolveSampling(const PreviewRequest &request) {
    return ResolveSampling(request, request.case_id == "CellularDet" ? 2 : 1);
}
} // namespace arch::api
