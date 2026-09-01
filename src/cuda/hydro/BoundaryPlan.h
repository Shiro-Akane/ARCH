#pragma once

#include "../common/CudaCommon.cuh"
#include "../../amr/BoundaryPlan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace arch::cuda
{
struct DeviceBoundaryTransfer {
    std::uint64_t logical_ordinal;
    int source_index;
    int destination_index;
    std::int8_t conserved_signs[5];
    std::int8_t species_sign;
};

struct DeviceCompiledBoundaryPlan {
    std::uint64_t logical_fingerprint;
    int total_size;
    std::array<boundary::BoundaryPhase, 3> phases;
    std::vector<DeviceBoundaryTransfer> transfers;
};

static_assert(std::is_standard_layout_v<DeviceBoundaryTransfer>);
static_assert(std::is_trivially_copyable_v<DeviceBoundaryTransfer>);

namespace detail
{
inline int flatten_boundary_checked(
    const DeviceGridView& grid, boundary::LogicalCellRef logical)
{
    const std::int64_t i = static_cast<std::int64_t>(grid.is) + logical.i;
    const std::int64_t j = static_cast<std::int64_t>(grid.js) + logical.j;
    const std::int64_t k = static_cast<std::int64_t>(grid.ks) + logical.k;
    if (i < 0 || i >= grid.total_x || j < 0 || j >= grid.total_y
        || k < 0 || k >= grid.total_z)
        throw std::out_of_range(
            "Boundary logical coordinate is outside device layout");
    const std::int64_t index = k * grid.stride_z + j * grid.stride_y + i;
    if (index < 0 || index >= grid.total_size
        || index > std::numeric_limits<int>::max())
        throw std::overflow_error(
            "Boundary device flattened index is invalid");
    return static_cast<int>(index);
}

ARCH_INLINE double boundary_signed_copy(
    double value, std::int8_t sign) noexcept
{
    return sign == -1 ? -value : value;
}
} // namespace detail

inline DeviceCompiledBoundaryPlan compile_boundary_plan(
    const boundary::BoundaryPlan& plan, const DeviceGridView& grid)
{
    const auto& input = plan.input();
    if (!valid_hydro_grid(grid)
        || grid.dim != input.dimension
        || grid.ng != static_cast<int>(input.ghost_depth)
        || grid.ie - grid.is != input.active_extent[0]
        || grid.je - grid.js != input.active_extent[1]
        || grid.ke - grid.ks != input.active_extent[2]
        || !boundary::detail::contains_active_region(
            grid.is, input.active_extent[0], grid.ng, grid.total_x)
        || !boundary::detail::contains_active_region(
            grid.js, input.active_extent[1], grid.dim >= 2 ? grid.ng : 0,
            grid.total_y)
        || !boundary::detail::contains_active_region(
            grid.ks, input.active_extent[2], grid.dim == 3 ? grid.ng : 0,
            grid.total_z))
        throw std::invalid_argument(
            "Device grid does not match logical boundary plan");

    DeviceCompiledBoundaryPlan compiled{};
    compiled.logical_fingerprint = plan.fingerprint();
    compiled.total_size = grid.total_size;
    for (std::size_t phase = 0; phase < compiled.phases.size(); ++phase)
        compiled.phases[phase] = plan.phases()[phase];
    compiled.transfers.reserve(plan.operations().size());
    for (const auto& operation : plan.operations()) {
        DeviceBoundaryTransfer transfer{};
        transfer.logical_ordinal = operation.ordinal;
        transfer.source_index = detail::flatten_boundary_checked(
            grid, operation.source);
        transfer.destination_index = detail::flatten_boundary_checked(
            grid, operation.destination);
        for (std::uint8_t field = 0; field < 5; ++field) {
            transfer.conserved_signs[field] = boundary::component_mapping(
                operation,
                static_cast<boundary::BoundaryFieldClass>(field)).sign;
        }
        transfer.species_sign = boundary::component_mapping(
            operation, boundary::BoundaryFieldClass::AllSpecies).sign;
        compiled.transfers.push_back(transfer);
    }
    return compiled;
}
} // namespace arch::cuda
