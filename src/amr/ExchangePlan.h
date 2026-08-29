/**
 * @file ExchangePlan.h
 * @brief Backend-neutral same-level ghost exchange authority.
 */

#pragma once

#include "BlockHandle.h"

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

namespace amr {

enum class ExchangeFace : std::uint8_t {
    X1Lower = 0,
    X1Upper = 1,
    X2Lower = 2,
    X2Upper = 3,
    X3Lower = 4,
    X3Upper = 5,
};

enum class ExchangePhaseId : std::uint8_t { X = 0, Y = 1, Z = 2 };

struct LogicalBlockKey {
    int dimension = 0;
    int level = 0;
    std::uint32_t logical_x1 = 0;
    std::uint32_t logical_x2 = 0;
    std::uint32_t logical_x3 = 0;
    friend constexpr auto operator<=>(const LogicalBlockKey&,
                                      const LogicalBlockKey&) = default;
};

struct ExchangeEndpoint {
    LogicalBlockKey logical{};
    BlockHandle handle{};
    friend constexpr bool operator==(const ExchangeEndpoint&,
                                     const ExchangeEndpoint&) = default;
};

struct LogicalExchangeBox {
    std::array<std::int32_t, 3> first{};
    std::array<std::uint32_t, 3> extent{};
    friend constexpr bool operator==(const LogicalExchangeBox&,
                                     const LogicalExchangeBox&) = default;
};

struct SameLevelExchangeOperation {
    std::uint64_t ordinal = 0;
    ExchangePhaseId phase = ExchangePhaseId::X;
    ExchangeEndpoint source{};
    ExchangeEndpoint destination{};
    LogicalExchangeBox source_box{};
    LogicalExchangeBox destination_box{};
    friend constexpr bool operator==(const SameLevelExchangeOperation&,
                                     const SameLevelExchangeOperation&) = default;
};

struct ExchangePhase {
    ExchangePhaseId id = ExchangePhaseId::X;
    std::size_t first = 0;
    std::size_t count = 0;
    friend constexpr bool operator==(const ExchangePhase&,
                                     const ExchangePhase&) = default;
};

struct SameLevelExchangePlan {
    TopologyEpoch epoch{};
    int dimension = 0;
    std::array<std::int32_t, 3> active_extent{};
    std::uint32_t ghost_depth = 0;
    std::array<ExchangePhase, 3> phases{};
    std::vector<ExchangeEndpoint> blocks;
    std::vector<SameLevelExchangeOperation> operations;
    std::uint64_t fingerprint = 0;
};

struct SameLevelTopologyEntry {
    LogicalBlockKey logical{};
    BlockHandle handle{};
    std::array<std::optional<LogicalBlockKey>, 6> neighbors{};
};

namespace exchange_detail {

constexpr int face_axis(ExchangeFace face) noexcept
{
    return static_cast<int>(face) / 2;
}

constexpr bool face_is_upper(ExchangeFace face) noexcept
{
    return (static_cast<int>(face) & 1) != 0;
}

constexpr ExchangeFace opposite_face(ExchangeFace face) noexcept
{
    return static_cast<ExchangeFace>(static_cast<int>(face) ^ 1);
}

inline std::uint64_t checked_product(
    std::array<std::uint32_t, 3> values)
{
    std::uint64_t result = 1;
    for (const std::uint32_t value : values) {
        if (value != 0
            && result > std::numeric_limits<std::uint64_t>::max() / value)
            throw std::overflow_error("same-level exchange extent overflow");
        result *= value;
    }
    return result;
}

class Fingerprint {
public:
    void byte(std::uint8_t value) noexcept
    {
        value_ ^= value;
        value_ *= UINT64_C(0x100000001b3);
    }

    void u32(std::uint32_t value) noexcept
    {
        for (int shift = 0; shift < 32; shift += 8)
            byte(static_cast<std::uint8_t>(value >> shift));
    }

    void i32(std::int32_t value) noexcept
    {
        u32(static_cast<std::uint32_t>(value));
    }

    void u64(std::uint64_t value) noexcept
    {
        for (int shift = 0; shift < 64; shift += 8)
            byte(static_cast<std::uint8_t>(value >> shift));
    }

    std::uint64_t value() const noexcept { return value_; }

private:
    std::uint64_t value_ = UINT64_C(0xcbf29ce484222325);
};

inline void hash_key(Fingerprint& hash, const LogicalBlockKey& key) noexcept
{
    hash.i32(key.dimension);
    hash.i32(key.level);
    hash.u32(key.logical_x1);
    hash.u32(key.logical_x2);
    hash.u32(key.logical_x3);
}

inline void hash_box(Fingerprint& hash, const LogicalExchangeBox& box) noexcept
{
    for (const auto value : box.first) hash.i32(value);
    for (const auto value : box.extent) hash.u32(value);
}

inline LogicalExchangeBox make_box(
    int dimension, const std::array<std::int32_t, 3>& active_extent,
    std::uint32_t ghost_depth, int phase_axis, bool source,
    bool destination_upper)
{
    LogicalExchangeBox box{};
    for (int axis = 0; axis < 3; ++axis) {
        if (axis >= dimension) {
            box.first[axis] = 0;
            box.extent[axis] = 1;
        } else if (axis == phase_axis) {
            box.extent[axis] = ghost_depth;
            if (source) {
                box.first[axis] = destination_upper
                    ? 0 : active_extent[axis]
                        - static_cast<std::int32_t>(ghost_depth);
            } else {
                box.first[axis] = destination_upper
                    ? active_extent[axis]
                    : -static_cast<std::int32_t>(ghost_depth);
            }
        } else if (axis < phase_axis) {
            box.first[axis] = -static_cast<std::int32_t>(ghost_depth);
            box.extent[axis] = static_cast<std::uint32_t>(
                active_extent[axis]) + 2U * ghost_depth;
        } else {
            box.first[axis] = 0;
            box.extent[axis] = static_cast<std::uint32_t>(
                active_extent[axis]);
        }
    }
    (void)checked_product(box.extent);
    return box;
}

} // namespace exchange_detail

inline std::uint64_t compute_same_level_exchange_fingerprint(
    const SameLevelExchangePlan& plan) noexcept
{
    exchange_detail::Fingerprint hash;
    hash.u32(1); // SameLevelExchangePlan contract version.
    hash.i32(plan.dimension);
    for (const auto value : plan.active_extent) hash.i32(value);
    hash.u32(plan.ghost_depth);
    hash.u64(static_cast<std::uint64_t>(plan.blocks.size()));
    for (const auto& block : plan.blocks)
        exchange_detail::hash_key(hash, block.logical);
    for (const auto& operation : plan.operations) {
        hash.u64(operation.ordinal);
        hash.byte(static_cast<std::uint8_t>(operation.phase));
        exchange_detail::hash_key(hash, operation.source.logical);
        exchange_detail::hash_key(hash, operation.destination.logical);
        exchange_detail::hash_box(hash, operation.source_box);
        exchange_detail::hash_box(hash, operation.destination_box);
    }
    return hash.value();
}

inline SameLevelExchangePlan make_same_level_exchange_plan(
    std::span<const SameLevelTopologyEntry> observations, int dimension,
    std::array<std::int32_t, 3> active_extent,
    std::uint32_t ghost_depth, TopologyEpoch epoch)
{
    if (dimension < 1 || dimension > 3)
        throw std::invalid_argument("exchange dimension must be in [1,3]");
    if (ghost_depth == 0
        || ghost_depth
            > static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()))
        throw std::invalid_argument("invalid same-level ghost depth");
    if (observations.empty())
        throw std::invalid_argument("exchange plan requires topology");
    const bool committed = is_valid(epoch);
    for (int axis = 0; axis < 3; ++axis) {
        if ((axis < dimension && active_extent[axis] <= 0)
            || (axis >= dimension && active_extent[axis] != 1)
            || (axis < dimension
                && ghost_depth > static_cast<std::uint32_t>(active_extent[axis])))
            throw std::invalid_argument("noncanonical exchange active extent");
    }

    std::map<LogicalBlockKey, SameLevelTopologyEntry> ordered;
    std::set<BlockHandle> handles;
    std::optional<int> level;
    for (const auto& entry : observations) {
        if (entry.logical.dimension != dimension
            || entry.logical.level < 0
            || (dimension < 2 && entry.logical.logical_x2 != 0)
            || (dimension < 3 && entry.logical.logical_x3 != 0)
            || (committed
                && (!is_valid(entry.handle) || entry.handle.epoch != epoch))
            || (!committed && is_valid(entry.handle)))
            throw std::invalid_argument("invalid same-level topology entry");
        if (level.has_value() && *level != entry.logical.level)
            throw std::invalid_argument("same-level plan received mixed levels");
        level = entry.logical.level;
        if (!ordered.emplace(entry.logical, entry).second)
            throw std::invalid_argument("duplicate logical exchange block");
        if (committed && !handles.insert(entry.handle).second)
            throw std::invalid_argument("duplicate exchange BlockHandle");
        for (int face = 2 * dimension; face < 6; ++face) {
            if (entry.neighbors[static_cast<std::size_t>(face)].has_value())
                throw std::invalid_argument("inactive exchange face has neighbor");
        }
    }

    for (const auto& [logical, entry] : ordered) {
        for (int face_value = 0; face_value < 2 * dimension; ++face_value) {
            const auto face = static_cast<ExchangeFace>(face_value);
            const auto& neighbor = entry.neighbors[
                static_cast<std::size_t>(face)];
            if (!neighbor.has_value()) continue;
            const auto found = ordered.find(*neighbor);
            if (found == ordered.end())
                throw std::invalid_argument("same-level neighbor is not active");
            const auto& reciprocal = found->second.neighbors[
                static_cast<std::size_t>(
                    exchange_detail::opposite_face(face))];
            if (!reciprocal.has_value() || *reciprocal != logical)
                throw std::invalid_argument("same-level neighbor is not reciprocal");
        }
    }

    SameLevelExchangePlan plan{};
    plan.epoch = epoch;
    plan.dimension = dimension;
    plan.active_extent = active_extent;
    plan.ghost_depth = ghost_depth;
    plan.blocks.reserve(ordered.size());
    for (const auto& [logical, entry] : ordered)
        plan.blocks.push_back({logical, entry.handle});
    std::uint64_t ordinal = 0;
    for (int phase_axis = 0; phase_axis < 3; ++phase_axis) {
        const auto phase = static_cast<ExchangePhaseId>(phase_axis);
        const std::size_t first = plan.operations.size();
        if (phase_axis < dimension) {
            for (const auto& [logical, destination] : ordered) {
                for (int side = 0; side < 2; ++side) {
                    const auto face = static_cast<ExchangeFace>(
                        2 * phase_axis + side);
                    const auto& neighbor = destination.neighbors[
                        static_cast<std::size_t>(face)];
                    if (!neighbor.has_value()) continue;
                    const auto& source = ordered.at(*neighbor);
                    const bool upper = exchange_detail::face_is_upper(face);
                    plan.operations.push_back({
                        ordinal++, phase,
                        ExchangeEndpoint{source.logical, source.handle},
                        ExchangeEndpoint{logical, destination.handle},
                        exchange_detail::make_box(
                            dimension, active_extent, ghost_depth,
                            phase_axis, true, upper),
                        exchange_detail::make_box(
                            dimension, active_extent, ghost_depth,
                            phase_axis, false, upper)});
                }
            }
        }
        plan.phases[phase_axis] = {
            phase, first, plan.operations.size() - first};
    }

    plan.fingerprint = compute_same_level_exchange_fingerprint(plan);
    if (plan.fingerprint == 0)
        throw std::overflow_error("same-level plan fingerprint is invalid");
    return plan;
}

struct HostExchangeLayout {
    int dimension = 0;
    std::array<std::int32_t, 3> active_origin{};
    std::array<std::int32_t, 3> total_extent{};
    std::array<std::int64_t, 3> stride{};
    std::int64_t total_size = 0;
    friend constexpr bool operator==(const HostExchangeLayout&,
                                     const HostExchangeLayout&) = default;
};

struct HostExchangeBlockView {
    LogicalBlockKey logical{};
    BlockHandle handle{};
    HostExchangeLayout layout{};
    std::array<double*, 6> conserved{};
    double* species = nullptr;
    int species_count = 0;
    std::int64_t species_stride = 0;
};

struct HostCompiledExchangeOperation {
    std::uint64_t ordinal = 0;
    ExchangePhaseId phase = ExchangePhaseId::X;
    std::size_t source_block = 0;
    std::size_t destination_block = 0;
    LogicalExchangeBox source_box{};
    LogicalExchangeBox destination_box{};
};

struct HostCompiledSameLevelExchangePlan {
    TopologyEpoch epoch{};
    std::uint64_t logical_fingerprint = 0;
    int species_count = 0;
    std::array<ExchangePhase, 3> phases{};
    std::vector<ExchangeEndpoint> blocks;
    std::vector<HostExchangeLayout> layouts;
    std::vector<HostCompiledExchangeOperation> operations;
};

namespace exchange_detail {

inline void validate_host_layout(
    const SameLevelExchangePlan& plan, const HostExchangeLayout& layout)
{
    if (layout.dimension != plan.dimension || layout.total_size <= 0
        || layout.stride[0] != 1 || layout.stride[1] <= 0
        || layout.stride[2] <= 0)
        throw std::invalid_argument("invalid Host exchange layout");
    for (int axis = 0; axis < 3; ++axis) {
        const std::int64_t origin = layout.active_origin[axis];
        const std::int64_t active = plan.active_extent[axis];
        const std::int64_t total = layout.total_extent[axis];
        const std::int64_t ghost = axis < plan.dimension
            ? static_cast<std::int64_t>(plan.ghost_depth) : 0;
        if (total <= 0 || origin < ghost
            || origin + active + ghost > total)
            throw std::invalid_argument(
                "Host layout does not contain exchange region");
    }
    if (layout.stride[1] < layout.total_extent[0]
        || layout.stride[2]
            < layout.stride[1] * layout.total_extent[1]
        || layout.total_size
            < layout.stride[2] * layout.total_extent[2])
        throw std::invalid_argument("Host exchange strides are undersized");
}

inline void validate_host_view(
    const SameLevelExchangePlan& plan, const HostExchangeBlockView& view)
{
    validate_host_layout(plan, view.layout);
    for (const double* field : view.conserved) {
        if (field == nullptr)
            throw std::invalid_argument("Host exchange field is null");
    }
    if (view.species_count < 0
        || (view.species_count == 0 && view.species != nullptr)
        || (view.species_count > 0
            && (view.species == nullptr
                || view.species_stride < view.layout.total_size)))
        throw std::invalid_argument("invalid Host exchange species view");
    if (view.species_count > 0
        && static_cast<std::uint64_t>(view.species_count)
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()
                / view.species_stride))
        throw std::overflow_error("Host species exchange extent overflow");
}

inline std::int64_t flatten_host_exchange(
    const HostExchangeLayout& layout,
    const std::array<std::int64_t, 3>& logical)
{
    std::array<std::int64_t, 3> physical{};
    for (int axis = 0; axis < 3; ++axis) {
        physical[axis] = layout.active_origin[axis] + logical[axis];
        if (physical[axis] < 0
            || physical[axis] >= layout.total_extent[axis])
            throw std::out_of_range("Host exchange cell is outside layout");
    }
    const std::int64_t index =
        physical[0] * layout.stride[0]
        + physical[1] * layout.stride[1]
        + physical[2] * layout.stride[2];
    if (index < 0 || index >= layout.total_size)
        throw std::out_of_range("Host exchange flattened index is invalid");
    return index;
}

template <class Function>
inline void for_each_box_cell(
    const LogicalExchangeBox& box, Function&& function)
{
    std::uint64_t linear = 0;
    for (std::uint32_t k = 0; k < box.extent[2]; ++k) {
        for (std::uint32_t j = 0; j < box.extent[1]; ++j) {
            for (std::uint32_t i = 0; i < box.extent[0]; ++i) {
                function(std::array<std::int64_t, 3>{
                             static_cast<std::int64_t>(box.first[0]) + i,
                             static_cast<std::int64_t>(box.first[1]) + j,
                             static_cast<std::int64_t>(box.first[2]) + k},
                         linear++);
            }
        }
    }
}

inline double* host_exchange_field(
    const HostExchangeBlockView& view, int field)
{
    if (field < 6) return view.conserved[static_cast<std::size_t>(field)];
    return view.species
        + static_cast<std::int64_t>(field - 6) * view.species_stride;
}

} // namespace exchange_detail

inline HostCompiledSameLevelExchangePlan compile_host_exchange_plan(
    const SameLevelExchangePlan& plan,
    std::span<const HostExchangeBlockView> views)
{
    if (plan.fingerprint == 0
        || compute_same_level_exchange_fingerprint(plan) != plan.fingerprint
        || views.size() != plan.blocks.size())
        throw std::invalid_argument("invalid Host logical exchange plan");
    HostCompiledSameLevelExchangePlan compiled{};
    compiled.epoch = plan.epoch;
    compiled.logical_fingerprint = plan.fingerprint;
    compiled.phases = plan.phases;
    compiled.blocks.reserve(views.size());
    compiled.layouts.resize(plan.blocks.size());

    std::map<LogicalBlockKey, std::size_t> indices;
    std::set<BlockHandle> handles;
    std::optional<int> species_count;
    for (std::size_t index = 0; index < views.size(); ++index) {
        const auto& view = views[index];
        exchange_detail::validate_host_view(plan, view);
        if (!indices.emplace(view.logical, index).second
            || (is_valid(plan.epoch)
                && !handles.insert(view.handle).second)
            || (is_valid(plan.epoch)
                && (!is_valid(view.handle)
                    || view.handle.epoch != plan.epoch))
            || (!is_valid(plan.epoch) && is_valid(view.handle)))
            throw std::invalid_argument("duplicate Host exchange endpoint");
        if (species_count.has_value()
            && *species_count != view.species_count)
            throw std::invalid_argument("Host exchange species counts differ");
        species_count = view.species_count;
        compiled.layouts[index] = view.layout;
        compiled.blocks.push_back({view.logical, view.handle});
    }
    compiled.species_count = species_count.value_or(0);
    for (const auto& block : plan.blocks) {
        const auto found = indices.find(block.logical);
        if (found == indices.end()
            || views[found->second].handle != block.handle)
            throw std::invalid_argument("Host exchange endpoint is missing");
    }

    compiled.operations.reserve(plan.operations.size());
    for (const auto& operation : plan.operations) {
        const auto source = indices.find(operation.source.logical);
        const auto destination = indices.find(operation.destination.logical);
        if (source == indices.end() || destination == indices.end()
            || views[source->second].handle != operation.source.handle
            || views[destination->second].handle
                != operation.destination.handle
            || operation.source_box.extent
                != operation.destination_box.extent)
            throw std::invalid_argument("Host exchange operation is stale");
        (void)exchange_detail::checked_product(operation.source_box.extent);
        exchange_detail::for_each_box_cell(
            operation.source_box, [&](const auto& logical, std::uint64_t) {
                (void)exchange_detail::flatten_host_exchange(
                    views[source->second].layout, logical);
            });
        exchange_detail::for_each_box_cell(
            operation.destination_box,
            [&](const auto& logical, std::uint64_t) {
                (void)exchange_detail::flatten_host_exchange(
                    views[destination->second].layout, logical);
            });
        compiled.operations.push_back({
            operation.ordinal, operation.phase, source->second,
            destination->second, operation.source_box,
            operation.destination_box});
    }
    return compiled;
}

inline void execute_host_exchange_plan(
    const HostCompiledSameLevelExchangePlan& compiled,
    std::span<const HostExchangeBlockView> views)
{
    if (compiled.logical_fingerprint == 0
        || views.size() != compiled.blocks.size()
        || compiled.layouts.size() != views.size()
        || compiled.species_count < 0)
        throw std::invalid_argument("invalid compiled Host exchange plan");
    for (std::size_t index = 0; index < views.size(); ++index) {
        if (views[index].logical != compiled.blocks[index].logical
            || views[index].handle != compiled.blocks[index].handle
            || views[index].layout != compiled.layouts[index]
            || views[index].species_count != compiled.species_count)
            throw std::invalid_argument("Host exchange view changed after compile");
    }

    // Validate the complete immutable lowering before the first destination
    // write.  In particular, a corrupted later phase may not leave an earlier
    // phase partially published.
    std::size_t validated_first = 0;
    for (std::size_t phase_index = 0;
         phase_index < compiled.phases.size(); ++phase_index) {
        const auto phase = compiled.phases[phase_index];
        if (phase.id != static_cast<ExchangePhaseId>(phase_index)
            || phase.first != validated_first
            || phase.first > compiled.operations.size()
            || phase.count > compiled.operations.size() - phase.first)
            throw std::invalid_argument("invalid Host exchange phase metadata");
        validated_first += phase.count;
        std::set<std::pair<std::size_t, std::int64_t>> destinations;
        for (std::size_t offset = 0; offset < phase.count; ++offset) {
            const auto& operation = compiled.operations[phase.first + offset];
            if (operation.ordinal != phase.first + offset
                || operation.phase != phase.id
                || operation.source_block >= views.size()
                || operation.destination_block >= views.size()
                || operation.source_box.extent
                    != operation.destination_box.extent)
                throw std::invalid_argument("invalid Host exchange operation");
            (void)exchange_detail::checked_product(
                operation.source_box.extent);
            exchange_detail::for_each_box_cell(
                operation.source_box,
                [&](const auto& logical, std::uint64_t) {
                    (void)exchange_detail::flatten_host_exchange(
                        views[operation.source_block].layout, logical);
                });
            exchange_detail::for_each_box_cell(
                operation.destination_box,
                [&](const auto& logical, std::uint64_t) {
                    const auto cell = exchange_detail::flatten_host_exchange(
                        views[operation.destination_block].layout, logical);
                    if (!destinations.emplace(
                            operation.destination_block, cell).second)
                        throw std::invalid_argument(
                            "duplicate Host exchange destination");
                });
        }
    }
    if (validated_first != compiled.operations.size())
        throw std::invalid_argument("Host exchange phases do not cover plan");

    std::size_t expected_first = 0;
    for (std::size_t phase_index = 0;
         phase_index < compiled.phases.size(); ++phase_index) {
        const auto phase = compiled.phases[phase_index];
        if (phase.id != static_cast<ExchangePhaseId>(phase_index)
            || phase.first != expected_first
            || phase.first > compiled.operations.size()
            || phase.count > compiled.operations.size() - phase.first)
            throw std::invalid_argument("invalid Host exchange phase metadata");
        expected_first += phase.count;

        std::vector<std::uint64_t> operation_offsets(phase.count + 1, 0);
        for (std::size_t offset = 0; offset < phase.count; ++offset) {
            const auto& operation = compiled.operations[phase.first + offset];
            if (operation.ordinal != phase.first + offset
                || operation.phase != phase.id
                || operation.source_block >= views.size()
                || operation.destination_block >= views.size()
                || operation.source_box.extent
                    != operation.destination_box.extent)
                throw std::invalid_argument("invalid Host exchange operation");
            const auto cells = exchange_detail::checked_product(
                operation.source_box.extent);
            if (operation_offsets[offset]
                > std::numeric_limits<std::uint64_t>::max() - cells)
                throw std::overflow_error("Host exchange phase size overflow");
            operation_offsets[offset + 1] = operation_offsets[offset] + cells;
        }
        const std::uint64_t cells = operation_offsets.back();
        const std::uint64_t fields =
            static_cast<std::uint64_t>(6 + compiled.species_count);
        if (fields != 0
            && cells > std::numeric_limits<std::size_t>::max() / fields)
            throw std::overflow_error("Host exchange scratch size overflow");
        std::vector<double> scratch(
            static_cast<std::size_t>(cells * fields));

        for (std::size_t offset = 0; offset < phase.count; ++offset) {
            const auto& operation = compiled.operations[phase.first + offset];
            const auto& source = views[operation.source_block];
            exchange_detail::for_each_box_cell(
                operation.source_box,
                [&](const auto& logical, std::uint64_t local) {
                    const auto source_index =
                        exchange_detail::flatten_host_exchange(
                            source.layout, logical);
                    for (int field = 0; field < 6 + compiled.species_count;
                         ++field) {
                        scratch[static_cast<std::size_t>(
                            static_cast<std::uint64_t>(field) * cells
                            + operation_offsets[offset] + local)] =
                            exchange_detail::host_exchange_field(
                                source, field)[source_index];
                    }
                });
        }
        for (std::size_t offset = 0; offset < phase.count; ++offset) {
            const auto& operation = compiled.operations[phase.first + offset];
            const auto& destination = views[operation.destination_block];
            exchange_detail::for_each_box_cell(
                operation.destination_box,
                [&](const auto& logical, std::uint64_t local) {
                    const auto destination_index =
                        exchange_detail::flatten_host_exchange(
                            destination.layout, logical);
                    for (int field = 0; field < 6 + compiled.species_count;
                         ++field) {
                        exchange_detail::host_exchange_field(
                            destination, field)[destination_index] =
                            scratch[static_cast<std::size_t>(
                                static_cast<std::uint64_t>(field) * cells
                                + operation_offsets[offset] + local)];
                    }
                });
        }
    }
    if (expected_first != compiled.operations.size())
        throw std::invalid_argument("Host exchange phases omit operations");
}

} // namespace amr
