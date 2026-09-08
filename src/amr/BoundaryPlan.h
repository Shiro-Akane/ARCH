/**
 * @file BoundaryPlan.h
 * @brief Backend-independent logical operations for physical ghost boundaries.
 *
 * Plans describe cell references, field classes, transfer weights, and axis
 * phases without embedding storage pointers. Host construction and validation
 * establish this contract before an executor binds the operations to memory.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace arch::boundary
{
enum class BoundaryType : std::uint8_t {
    Periodic = 0,
    Outflow = 1,
    Reflecting = 2,
    Inactive = 3,
};

enum class BoundaryAxis : std::uint8_t { X1 = 0, X2 = 1, X3 = 2 };
enum class BoundarySide : std::uint8_t { Lower = 0, Upper = 1 };
enum class BoundaryPhaseId : std::uint8_t { X = 0, Y = 1, Z = 2 };

enum class BoundaryFieldClass : std::uint8_t {
    Density = 0,
    MomentumX = 1,
    MomentumY = 2,
    MomentumZ = 3,
    Energy = 4,
    AllSpecies = 5,
};

struct LogicalCellRef {
    std::int32_t i;
    std::int32_t j;
    std::int32_t k;

    friend constexpr bool operator==(
        const LogicalCellRef&, const LogicalCellRef&) = default;
};

struct ExactTransferWeight {
    std::int32_t numerator;
    std::int32_t denominator;
};

struct ComponentMapping {
    BoundaryFieldClass destination;
    BoundaryFieldClass source;
    std::int8_t sign;
};

struct BoundaryOperation {
    std::uint64_t ordinal;
    LogicalCellRef source;
    LogicalCellRef destination;
    BoundaryAxis axis;
    BoundarySide side;
    BoundaryPhaseId phase;
    std::uint32_t depth;
    BoundaryType type;
    ExactTransferWeight weight;
};

struct BoundaryPhase {
    BoundaryPhaseId id;
    std::size_t first;
    std::size_t count;

    friend constexpr bool operator==(
        const BoundaryPhase&, const BoundaryPhase&) = default;
};

struct BoundaryPlanInput {
    int dimension;
    std::array<std::int32_t, 3> active_extent;
    std::uint32_t ghost_depth;
    std::array<BoundaryType, 6> faces;
};

constexpr std::size_t face_index(
    BoundaryAxis axis, BoundarySide side) noexcept
{
    return 2U * static_cast<std::size_t>(axis)
        + static_cast<std::size_t>(side);
}

inline BoundaryType face_type(
    const BoundaryPlanInput& input, BoundaryAxis axis,
    BoundarySide side) noexcept
{
    const auto axis_value = static_cast<std::uint8_t>(axis);
    const auto side_value = static_cast<std::uint8_t>(side);
    if (axis_value > static_cast<std::uint8_t>(BoundaryAxis::X3)
        || side_value > static_cast<std::uint8_t>(BoundarySide::Upper))
        return static_cast<BoundaryType>(std::numeric_limits<std::uint8_t>::max());
    return input.faces[face_index(axis, side)];
}

inline std::int8_t reflection_sign(
    BoundaryAxis axis, BoundaryType type,
    BoundaryFieldClass field) noexcept
{
    if (type != BoundaryType::Reflecting)
        return 1;
    const bool normal =
        (axis == BoundaryAxis::X1 && field == BoundaryFieldClass::MomentumX)
        || (axis == BoundaryAxis::X2 && field == BoundaryFieldClass::MomentumY)
        || (axis == BoundaryAxis::X3 && field == BoundaryFieldClass::MomentumZ);
    return normal ? static_cast<std::int8_t>(-1)
                  : static_cast<std::int8_t>(1);
}

inline ComponentMapping component_mapping(
    const BoundaryOperation& operation, BoundaryFieldClass field)
{
    const auto value = static_cast<std::uint8_t>(field);
    if (value > static_cast<std::uint8_t>(BoundaryFieldClass::AllSpecies))
        throw std::invalid_argument("Unknown boundary field class");
    return {field, field, reflection_sign(operation.axis, operation.type, field)};
}

namespace detail
{
constexpr bool contains_active_region(
    int origin, int active_extent, int ghost_depth, int total_extent) noexcept
{
    const auto origin64 = static_cast<std::int64_t>(origin);
    const auto active64 = static_cast<std::int64_t>(active_extent);
    const auto ghost64 = static_cast<std::int64_t>(ghost_depth);
    const auto total64 = static_cast<std::int64_t>(total_extent);
    return origin64 >= ghost64
        && origin64 + active64 + ghost64 <= total64;
}

inline bool valid_boundary_type(BoundaryType type) noexcept
{
    return static_cast<std::uint8_t>(type)
        <= static_cast<std::uint8_t>(BoundaryType::Inactive);
}

inline std::int32_t checked_destination_coordinate(
    std::int32_t extent, std::uint32_t depth, BoundarySide side)
{
    if (depth > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        throw std::overflow_error("Boundary ghost coordinate overflows int32");
    const auto depth64 = static_cast<std::int64_t>(depth);
    const auto coordinate = side == BoundarySide::Lower
        ? -depth64
        : static_cast<std::int64_t>(extent) - 1 + depth64;
    if (coordinate < std::numeric_limits<std::int32_t>::min()
        || coordinate > std::numeric_limits<std::int32_t>::max())
        throw std::overflow_error("Boundary destination coordinate overflows int32");
    return static_cast<std::int32_t>(coordinate);
}

inline std::int32_t source_axis_coordinate(
    std::int32_t extent, std::uint32_t depth, BoundarySide side,
    BoundaryType type)
{
    if (extent <= 0 || depth == 0
        || depth > static_cast<std::uint32_t>(
            std::numeric_limits<std::int32_t>::max()))
        throw std::invalid_argument("Invalid boundary source coordinate input");

    const auto extent64 = static_cast<std::int64_t>(extent);
    const auto depth64 = static_cast<std::int64_t>(depth);
    if (type != BoundaryType::Outflow && depth64 > extent64)
        throw std::invalid_argument("Boundary source depth exceeds extent");
    std::int64_t coordinate = 0;
    if (type == BoundaryType::Periodic) {
        coordinate = side == BoundarySide::Lower
            ? extent64 - depth64 : depth64 - 1;
    } else if (type == BoundaryType::Outflow) {
        coordinate = side == BoundarySide::Lower ? 0 : extent64 - 1;
    } else if (type == BoundaryType::Reflecting) {
        coordinate = side == BoundarySide::Lower
            ? depth64 - 1 : extent64 - depth64;
    } else {
        throw std::invalid_argument(
            "Inactive boundary cannot produce an operation");
    }
    if (coordinate < std::numeric_limits<std::int32_t>::min()
        || coordinate > std::numeric_limits<std::int32_t>::max())
        throw std::overflow_error("Boundary source coordinate overflows int32");
    return static_cast<std::int32_t>(coordinate);
}

inline std::uint64_t checked_multiply(
    std::uint64_t left, std::uint64_t right)
{
    if (right != 0 && left > std::numeric_limits<std::uint64_t>::max() / right)
        throw std::overflow_error("Boundary operation count overflow");
    return left * right;
}

inline std::uint64_t checked_add(std::uint64_t left, std::uint64_t right)
{
    if (left > std::numeric_limits<std::uint64_t>::max() - right)
        throw std::overflow_error("Boundary operation count overflow");
    return left + right;
}

class Fingerprint
{
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
} // namespace detail

inline LogicalCellRef source_logical_cell(
    const BoundaryPlanInput& input, BoundaryAxis axis, BoundarySide side,
    std::uint32_t depth, LogicalCellRef destination)
{
    const auto axis_value = static_cast<std::uint8_t>(axis);
    const auto side_value = static_cast<std::uint8_t>(side);
    if (axis_value > static_cast<std::uint8_t>(BoundaryAxis::X3)
        || side_value > static_cast<std::uint8_t>(BoundarySide::Upper)
        || axis_value >= input.dimension
        || depth == 0 || depth > input.ghost_depth)
        throw std::invalid_argument("Invalid typed boundary source request");
    const auto type = face_type(input, axis, side);
    if (!detail::valid_boundary_type(type) || type == BoundaryType::Inactive)
        throw std::invalid_argument("Invalid active boundary type");
    LogicalCellRef source = destination;
    const auto coordinate = detail::source_axis_coordinate(
        input.active_extent[axis_value], depth, side, type);
    if (axis == BoundaryAxis::X1)
        source.i = coordinate;
    else if (axis == BoundaryAxis::X2)
        source.j = coordinate;
    else
        source.k = coordinate;
    return source;
}

class BoundaryPlan
{
public:
    const BoundaryPlanInput& input() const noexcept { return input_; }
    std::span<const BoundaryPhase> phases() const noexcept { return phases_; }
    std::span<const BoundaryOperation> operations() const noexcept
    {
        return operations_;
    }
    std::uint64_t fingerprint() const noexcept { return fingerprint_; }

private:
    BoundaryPlanInput input_{};
    std::array<BoundaryPhase, 3> phases_{};
    std::vector<BoundaryOperation> operations_;
    std::uint64_t fingerprint_{};

    friend BoundaryPlan make_boundary_plan(const BoundaryPlanInput&);
};

inline BoundaryPlan make_boundary_plan(const BoundaryPlanInput& input)
{
    if (input.dimension < 1 || input.dimension > 3)
        throw std::invalid_argument("Boundary dimension must be in [1,3]");
    if (input.ghost_depth == 0)
        throw std::invalid_argument("Boundary ghost depth must be positive");

    for (int axis = 0; axis < 3; ++axis) {
        const bool active = axis < input.dimension;
        const auto extent = input.active_extent[axis];
        if ((active && extent <= 0) || (!active && extent != 1))
            throw std::invalid_argument("Noncanonical boundary active extent");
        for (int side = 0; side < 2; ++side) {
            const auto type = face_type(
                input, static_cast<BoundaryAxis>(axis),
                static_cast<BoundarySide>(side));
            if (!detail::valid_boundary_type(type))
                throw std::invalid_argument("Unknown boundary type");
            if (active && type == BoundaryType::Inactive)
                throw std::invalid_argument("Inactive type on active axis");
            if (!active && type != BoundaryType::Inactive)
                throw std::invalid_argument("Noncanonical inactive boundary face");
            if (active && type != BoundaryType::Outflow
                && input.ghost_depth > static_cast<std::uint32_t>(extent))
                throw std::invalid_argument("Ghost depth exceeds mapped extent");
        }
        if (active) {
            (void)detail::checked_destination_coordinate(
                extent, input.ghost_depth, BoundarySide::Upper);
        }
    }

    const auto nx = static_cast<std::uint64_t>(input.active_extent[0]);
    const auto ny = static_cast<std::uint64_t>(input.active_extent[1]);
    const auto nz = static_cast<std::uint64_t>(input.active_extent[2]);
    const auto ng = static_cast<std::uint64_t>(input.ghost_depth);
    const auto sides_and_depth = detail::checked_multiply(2, ng);
    const auto x_count = detail::checked_multiply(
        detail::checked_multiply(ny, nz), sides_and_depth);
    const auto full_x = detail::checked_add(nx, detail::checked_multiply(2, ng));
    const auto y_count = input.dimension >= 2
        ? detail::checked_multiply(
            detail::checked_multiply(full_x, nz), sides_and_depth)
        : 0;
    const auto full_y = detail::checked_add(ny, detail::checked_multiply(2, ng));
    const auto z_count = input.dimension == 3
        ? detail::checked_multiply(
            detail::checked_multiply(full_x, full_y), sides_and_depth)
        : 0;
    const auto total_count = detail::checked_add(
        detail::checked_add(x_count, y_count), z_count);
    if (total_count > std::numeric_limits<std::size_t>::max()
        || total_count > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
        || total_count > std::vector<BoundaryOperation>{}.max_size())
        throw std::overflow_error("Boundary operation storage overflow");

    BoundaryPlan plan;
    plan.input_ = input;
    plan.operations_.reserve(static_cast<std::size_t>(total_count));

    const auto append = [&](BoundaryAxis axis, BoundarySide side,
                            BoundaryPhaseId phase, std::uint32_t depth,
                            LogicalCellRef destination) {
        const auto type = face_type(input, axis, side);
        BoundaryOperation operation{};
        operation.ordinal = plan.operations_.size();
        operation.destination = destination;
        operation.source = source_logical_cell(
            input, axis, side, depth, destination);
        operation.axis = axis;
        operation.side = side;
        operation.phase = phase;
        operation.depth = depth;
        operation.type = type;
        operation.weight = {1, 1};
        plan.operations_.push_back(operation);
    };

    const auto phase_begin = [&](BoundaryPhaseId id) {
        return BoundaryPhase{id, plan.operations_.size(), 0};
    };
    plan.phases_[0] = phase_begin(BoundaryPhaseId::X);
    for (std::int32_t k = 0; k < input.active_extent[2]; ++k)
        for (std::int32_t j = 0; j < input.active_extent[1]; ++j)
            for (std::uint32_t depth = 1; depth <= input.ghost_depth; ++depth)
                for (BoundarySide side : {BoundarySide::Lower, BoundarySide::Upper}) {
                    LogicalCellRef destination{0, j, k};
                    destination.i = detail::checked_destination_coordinate(
                        input.active_extent[0], depth, side);
                    append(BoundaryAxis::X1, side, BoundaryPhaseId::X,
                           depth, destination);
                }
    plan.phases_[0].count = plan.operations_.size() - plan.phases_[0].first;

    plan.phases_[1] = phase_begin(BoundaryPhaseId::Y);
    if (input.dimension >= 2) {
        const auto begin_i = -static_cast<std::int32_t>(input.ghost_depth);
        const auto end_i = detail::checked_destination_coordinate(
            input.active_extent[0], input.ghost_depth, BoundarySide::Upper) + 1;
        for (std::int32_t k = 0; k < input.active_extent[2]; ++k)
            for (std::int32_t i = begin_i; i < end_i; ++i)
                for (std::uint32_t depth = 1; depth <= input.ghost_depth; ++depth)
                    for (BoundarySide side : {BoundarySide::Lower, BoundarySide::Upper}) {
                        LogicalCellRef destination{i, 0, k};
                        destination.j = detail::checked_destination_coordinate(
                            input.active_extent[1], depth, side);
                        append(BoundaryAxis::X2, side, BoundaryPhaseId::Y,
                               depth, destination);
                    }
    }
    plan.phases_[1].count = plan.operations_.size() - plan.phases_[1].first;

    plan.phases_[2] = phase_begin(BoundaryPhaseId::Z);
    if (input.dimension == 3) {
        const auto begin_i = -static_cast<std::int32_t>(input.ghost_depth);
        const auto end_i = detail::checked_destination_coordinate(
            input.active_extent[0], input.ghost_depth, BoundarySide::Upper) + 1;
        const auto begin_j = -static_cast<std::int32_t>(input.ghost_depth);
        const auto end_j = detail::checked_destination_coordinate(
            input.active_extent[1], input.ghost_depth, BoundarySide::Upper) + 1;
        for (std::int32_t j = begin_j; j < end_j; ++j)
            for (std::int32_t i = begin_i; i < end_i; ++i)
                for (std::uint32_t depth = 1; depth <= input.ghost_depth; ++depth)
                    for (BoundarySide side : {BoundarySide::Lower, BoundarySide::Upper}) {
                        LogicalCellRef destination{i, j, 0};
                        destination.k = detail::checked_destination_coordinate(
                            input.active_extent[2], depth, side);
                        append(BoundaryAxis::X3, side, BoundaryPhaseId::Z,
                               depth, destination);
                    }
    }
    plan.phases_[2].count = plan.operations_.size() - plan.phases_[2].first;

    if (plan.operations_.size() != total_count)
        throw std::logic_error("Boundary operation count construction drifted");

    detail::Fingerprint hash;
    hash.u32(1);
    hash.i32(input.dimension);
    for (const auto extent : input.active_extent)
        hash.i32(extent);
    hash.u32(input.ghost_depth);
    for (const auto face : input.faces)
        hash.byte(static_cast<std::uint8_t>(face));
    for (const auto& phase : plan.phases_) {
        hash.byte(static_cast<std::uint8_t>(phase.id));
        hash.u64(static_cast<std::uint64_t>(phase.first));
        hash.u64(static_cast<std::uint64_t>(phase.count));
    }
    for (const auto& operation : plan.operations_) {
        hash.u64(operation.ordinal);
        hash.i32(operation.source.i);
        hash.i32(operation.source.j);
        hash.i32(operation.source.k);
        hash.i32(operation.destination.i);
        hash.i32(operation.destination.j);
        hash.i32(operation.destination.k);
        hash.byte(static_cast<std::uint8_t>(operation.axis));
        hash.byte(static_cast<std::uint8_t>(operation.side));
        hash.byte(static_cast<std::uint8_t>(operation.phase));
        hash.u32(operation.depth);
        hash.byte(static_cast<std::uint8_t>(operation.type));
        hash.i32(operation.weight.numerator);
        hash.i32(operation.weight.denominator);
        for (std::uint8_t field = 0;
             field <= static_cast<std::uint8_t>(BoundaryFieldClass::AllSpecies);
             ++field) {
            const auto mapping = component_mapping(
                operation, static_cast<BoundaryFieldClass>(field));
            hash.byte(static_cast<std::uint8_t>(mapping.destination));
            hash.byte(static_cast<std::uint8_t>(mapping.source));
            hash.byte(static_cast<std::uint8_t>(mapping.sign));
        }
    }
    plan.fingerprint_ = hash.value();
    return plan;
}

static_assert(static_cast<std::uint8_t>(BoundaryType::Periodic) == 0);
static_assert(static_cast<std::uint8_t>(BoundaryType::Outflow) == 1);
static_assert(static_cast<std::uint8_t>(BoundaryType::Reflecting) == 2);
static_assert(static_cast<std::uint8_t>(BoundaryType::Inactive) == 3);
static_assert(face_index(BoundaryAxis::X1, BoundarySide::Lower) == 0);
static_assert(face_index(BoundaryAxis::X3, BoundarySide::Upper) == 5);
} // namespace arch::boundary
