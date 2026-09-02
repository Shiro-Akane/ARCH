/**
 * @file CoarseFineCellPlan.h
 * @brief CPU-only lowering of logical 2:1 ghost transfers to cell records.
 *
 * The logical AMR operation plan remains the source of truth.  This lowering
 * performs all coarse/fine index mathematics on the Host; backends only map
 * the resulting relative cell coordinates to their own storage and execute
 * copies or averages.
 */

#pragma once

#include "AmrTransferPlans.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace amr {

using LogicalAmrCell = std::array<std::int32_t, 3>;

struct CoarseFineCellTransfer {
    AmrEndpoint source{};
    AmrEndpoint destination{};
    LogicalAmrCell destination_cell{};
    std::array<LogicalAmrCell, 8> source_cells{};
    std::uint8_t source_count = 0;
    RefinementRule rule = RefinementRule::CoarseGhostInjection;
};

struct CoarseFineCellPlan {
    int dimension = 0;
    int species_count = 0;
    AmrPlanScope scope{};
    std::uint64_t logical_fingerprint = 0;
    std::vector<CoarseFineCellTransfer> transfers;
};

namespace coarse_fine_detail {

inline std::int32_t checked_coordinate(
    std::int32_t first, std::uint64_t offset)
{
    if (offset > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()))
        throw std::overflow_error("coarse-fine cell coordinate overflow");
    const std::int64_t value = static_cast<std::int64_t>(first)
        + static_cast<std::int64_t>(offset);
    if (value < std::numeric_limits<std::int32_t>::min()
        || value > std::numeric_limits<std::int32_t>::max())
        throw std::overflow_error("coarse-fine cell coordinate overflow");
    return static_cast<std::int32_t>(value);
}

inline LogicalAmrCell cell_from_box(
    const LogicalAmrBox& box, std::uint64_t linear)
{
    LogicalAmrCell cell{};
    for (int axis = 0; axis < 3; ++axis) {
        const std::uint32_t extent = box.extent[axis];
        if (extent == 0)
            throw std::invalid_argument(
                "coarse-fine cell box has zero extent");
        const std::uint64_t offset = linear % extent;
        linear /= extent;
        cell[axis] = checked_coordinate(box.first[axis], offset);
    }
    if (linear != 0)
        throw std::out_of_range("coarse-fine cell ordinal exceeds box");
    return cell;
}

inline bool contains(
    const LogicalAmrBox& box, const LogicalAmrCell& cell) noexcept
{
    for (int axis = 0; axis < 3; ++axis) {
        const std::int64_t first = box.first[axis];
        const std::int64_t end = first + box.extent[axis];
        if (cell[axis] < first || cell[axis] >= end) return false;
    }
    return true;
}

inline void validate_field_group(
    const CoarseFineTransferPlan& plan, std::size_t first,
    int species_count)
{
    const std::size_t field_count = static_cast<std::size_t>(
        6 + species_count);
    if (field_count > plan.operations.size() - first)
        throw std::invalid_argument(
            "coarse-fine field group is incomplete");
    const AmrTransferOperation& head = plan.operations[first];
    if (head.source.handle == head.destination.handle)
        throw std::invalid_argument(
            "coarse-fine source and destination identities alias");
    for (std::size_t offset = 0; offset < field_count; ++offset) {
        const AmrTransferOperation& operation =
            plan.operations[first + offset];
        const AmrField expected_field = offset < 6
            ? static_cast<AmrField>(offset) : AmrField::Species;
        const int expected_component = offset < 6
            ? -1 : static_cast<int>(offset - 6);
        if (operation.source != head.source
            || operation.destination != head.destination
            || operation.source_box != head.source_box
            || operation.destination_box != head.destination_box
            || operation.axis != head.axis
            || operation.side != head.side
            || operation.rule != head.rule
            || operation.field != expected_field
            || operation.component != expected_component
            || operation.weight != 1.0 || operation.sign != 1.0)
            throw std::invalid_argument(
                "coarse-fine field group is noncanonical");
    }
}

inline void append_injection_transfers(
    CoarseFineCellPlan& compiled, const AmrTransferOperation& operation,
    std::set<std::tuple<AmrEndpoint, LogicalAmrCell>>& destinations)
{
    const std::uint64_t cells =
        amr_plan_detail::checked_box_cells(operation.destination_box);
    if (cells > compiled.transfers.max_size() - compiled.transfers.size())
        throw std::overflow_error("coarse-fine cell plan is too large");
    compiled.transfers.reserve(
        compiled.transfers.size() + static_cast<std::size_t>(cells));
    for (std::uint64_t linear = 0; linear < cells; ++linear) {
        CoarseFineCellTransfer transfer{};
        transfer.source = operation.source;
        transfer.destination = operation.destination;
        transfer.destination_cell = cell_from_box(
            operation.destination_box, linear);
        transfer.source_count = 1;
        transfer.rule = operation.rule;
        for (int axis = 0; axis < 3; ++axis) {
            const std::uint64_t destination_offset =
                static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(transfer.destination_cell[axis])
                    - operation.destination_box.first[axis]);
            transfer.source_cells[0][axis] = checked_coordinate(
                operation.source_box.first[axis], destination_offset / 2);
        }
        if (!contains(operation.source_box, transfer.source_cells[0])
            || !destinations.emplace(
                transfer.destination, transfer.destination_cell).second)
            throw std::invalid_argument(
                "coarse-fine injection boxes overlap or do not map 2:1");
        compiled.transfers.push_back(transfer);
    }
}

inline void append_average_transfers(
    CoarseFineCellPlan& compiled, const AmrTransferOperation& operation,
    std::set<std::tuple<AmrEndpoint, LogicalAmrCell>>& destinations)
{
    const std::uint64_t cells =
        amr_plan_detail::checked_box_cells(operation.destination_box);
    if (cells > compiled.transfers.max_size() - compiled.transfers.size())
        throw std::overflow_error("coarse-fine cell plan is too large");
    compiled.transfers.reserve(
        compiled.transfers.size() + static_cast<std::size_t>(cells));
    const int source_count = 1 << compiled.dimension;
    for (std::uint64_t linear = 0; linear < cells; ++linear) {
        CoarseFineCellTransfer transfer{};
        transfer.source = operation.source;
        transfer.destination = operation.destination;
        transfer.destination_cell = cell_from_box(
            operation.destination_box, linear);
        transfer.source_count = static_cast<std::uint8_t>(source_count);
        transfer.rule = operation.rule;

        LogicalAmrCell source_base{};
        for (int axis = 0; axis < 3; ++axis) {
            const std::uint64_t destination_offset =
                static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(transfer.destination_cell[axis])
                    - operation.destination_box.first[axis]);
            if (destination_offset
                > std::numeric_limits<std::uint64_t>::max() / 2)
                throw std::overflow_error(
                    "coarse-fine average offset overflow");
            source_base[axis] = checked_coordinate(
                operation.source_box.first[axis], 2 * destination_offset);
        }

        int source_index = 0;
        for (int z = 0; z < (compiled.dimension == 3 ? 2 : 1); ++z) {
            for (int y = 0; y < (compiled.dimension >= 2 ? 2 : 1); ++y) {
                for (int x = 0; x < 2; ++x) {
                    LogicalAmrCell source = source_base;
                    source[0] = checked_coordinate(source[0], x);
                    source[1] = checked_coordinate(source[1], y);
                    source[2] = checked_coordinate(source[2], z);
                    if (!contains(operation.source_box, source))
                        throw std::invalid_argument(
                            "coarse-fine average boxes do not map 2:1");
                    transfer.source_cells[source_index++] = source;
                }
            }
        }
        if (source_index != source_count
            || !destinations.emplace(
                transfer.destination, transfer.destination_cell).second)
            throw std::invalid_argument(
                "coarse-fine average destinations overlap");
        compiled.transfers.push_back(transfer);
    }
}

} // namespace coarse_fine_detail

/**
 * @brief Validate and lower a logical coarse/fine plan to cell operations.
 *
 * Coordinates are relative to each block's active-cell origin.  Injection
 * records contain one source cell; fine averages contain 2^dimension source
 * cells.  Destination cells are required to be unique so the backend scatter
 * phase is race-free.
 */
inline CoarseFineCellPlan compile_coarse_fine_cell_plan(
    const CoarseFineTransferPlan& plan, int species_count)
{
    validate_amr_plan(plan);
    if (species_count < 0
        || species_count > std::numeric_limits<int>::max() - 6)
        throw std::invalid_argument(
            "coarse-fine lowering has an invalid species count");

    CoarseFineCellPlan compiled{};
    compiled.dimension = plan.dimension;
    compiled.species_count = species_count;
    compiled.scope = plan.scope;
    compiled.logical_fingerprint = plan.fingerprint;
    std::set<std::tuple<AmrEndpoint, LogicalAmrCell>> destinations;

    const std::size_t field_count = static_cast<std::size_t>(
        6 + species_count);
    for (std::size_t first = 0; first < plan.operations.size();
         first += field_count) {
        coarse_fine_detail::validate_field_group(
            plan, first, species_count);
        const AmrTransferOperation& operation = plan.operations[first];
        if (operation.rule == RefinementRule::CoarseGhostInjection) {
            coarse_fine_detail::append_injection_transfers(
                compiled, operation, destinations);
        } else if (operation.rule == RefinementRule::FineGhostAverage) {
            coarse_fine_detail::append_average_transfers(
                compiled, operation, destinations);
        } else {
            throw std::invalid_argument(
                "coarse-fine cell plan has an unsupported rule");
        }
    }
    return compiled;
}

} // namespace amr
