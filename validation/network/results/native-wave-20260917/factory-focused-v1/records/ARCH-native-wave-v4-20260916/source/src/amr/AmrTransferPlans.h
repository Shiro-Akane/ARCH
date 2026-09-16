/**
 * @file AmrTransferPlans.h
 * @brief Backend-neutral logical AMR transfer and reflux plans.
 */

#pragma once

#include "BlockHandle.h"
#include "ExchangePlan.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

namespace amr {

enum class AmrAxis : std::uint8_t { X = 0, Y = 1, Z = 2 };
enum class AmrSide : std::uint8_t { Lower = 0, Upper = 1 };

enum class AmrField : std::uint8_t {
    Rho = 0,
    MomU = 1,
    MomV = 2,
    MomW = 3,
    Energy = 4,
    EnucRate = 5,
    Species = 6,
};

enum class RefinementRule : std::uint8_t {
    CoarseGhostInjection = 0,
    FineGhostAverage = 1,
    ConservativeMinmodProlongation = 2,
    VolumeRestriction = 3,
    FineFluxContribution = 4,
    CoarseFluxContribution = 5,
    RefluxCorrection = 6,
};

enum class AmrPlanKind : std::uint8_t {
    CoarseFineTransfer = 0,
    Prolongation = 1,
    Restriction = 2,
    FluxRegistration = 3,
    Reflux = 4,
};

struct LogicalAmrBox {
    std::array<std::int32_t, 3> first{};
    std::array<std::uint32_t, 3> extent{};
    friend constexpr auto operator<=>(const LogicalAmrBox&,
                                      const LogicalAmrBox&) = default;
};

struct AmrEndpoint {
    LogicalBlockKey logical{};
    BlockHandle handle{};
    friend constexpr auto operator<=>(const AmrEndpoint&,
                                      const AmrEndpoint&) = default;
};

struct AmrPlanScope {
    std::uint64_t transaction_id = 0;
    TopologyEpoch from_epoch{};
    TopologyEpoch to_epoch{};
    friend constexpr auto operator<=>(const AmrPlanScope&,
                                      const AmrPlanScope&) = default;
};

struct AmrTransferOperation {
    std::uint64_t ordinal = 0;
    AmrEndpoint source{};
    AmrEndpoint destination{};
    LogicalAmrBox source_box{};
    LogicalAmrBox destination_box{};
    AmrAxis axis = AmrAxis::X;
    AmrSide side = AmrSide::Lower;
    AmrField field = AmrField::Rho;
    std::int32_t component = -1;
    RefinementRule rule = RefinementRule::CoarseGhostInjection;
    double weight = 1.0;
    double sign = 1.0;
    friend constexpr bool operator==(const AmrTransferOperation&,
                                     const AmrTransferOperation&) = default;
};

template <AmrPlanKind Kind>
struct BasicAmrPlan {
    static constexpr AmrPlanKind kind = Kind;
    int dimension = 0;
    AmrPlanScope scope{};
    std::vector<AmrTransferOperation> operations;
    std::uint64_t fingerprint = 0;
};

using CoarseFineTransferPlan = BasicAmrPlan<AmrPlanKind::CoarseFineTransfer>;
using ProlongationPlan = BasicAmrPlan<AmrPlanKind::Prolongation>;
using RestrictionPlan = BasicAmrPlan<AmrPlanKind::Restriction>;
using FluxRegistrationPlan = BasicAmrPlan<AmrPlanKind::FluxRegistration>;
using RefluxPlan = BasicAmrPlan<AmrPlanKind::Reflux>;

constexpr bool is_migration_plan(AmrPlanKind kind) noexcept
{
    return kind == AmrPlanKind::Prolongation
        || kind == AmrPlanKind::Restriction;
}

constexpr int axis_value(AmrAxis axis) noexcept
{
    return static_cast<int>(axis);
}

constexpr int side_value(AmrSide side) noexcept
{
    return static_cast<int>(side);
}

namespace amr_plan_detail {

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

inline void hash_logical_key(Fingerprint& hash,
                             const LogicalBlockKey& key) noexcept
{
    hash.i32(key.dimension);
    hash.i32(key.level);
    hash.u32(key.logical_x1);
    hash.u32(key.logical_x2);
    hash.u32(key.logical_x3);
}

inline void hash_endpoint(Fingerprint& hash,
                          const AmrEndpoint& endpoint) noexcept
{
    hash_logical_key(hash, endpoint.logical);
    hash.u64(endpoint.handle.uid.value);
    hash.u64(endpoint.handle.epoch.value);
}

inline void hash_box(Fingerprint& hash, const LogicalAmrBox& box) noexcept
{
    for (const auto value : box.first) hash.i32(value);
    for (const auto value : box.extent) hash.u32(value);
}

inline std::uint64_t checked_box_cells(const LogicalAmrBox& box)
{
    std::uint64_t cells = 1;
    for (const std::uint32_t extent : box.extent) {
        if (extent == 0)
            throw std::invalid_argument("AMR logical box has zero extent");
        if (cells > std::numeric_limits<std::uint64_t>::max() / extent)
            throw std::overflow_error("AMR logical box extent overflow");
        cells *= extent;
    }
    return cells;
}

constexpr bool is_finite_binary64(double value) noexcept
{
    constexpr std::uint64_t exponent_mask = UINT64_C(0x7ff0000000000000);
    return (std::bit_cast<std::uint64_t>(value) & exponent_mask)
        != exponent_mask;
}

inline void validate_logical_key(const LogicalBlockKey& key, int dimension)
{
    if (key.dimension != dimension || key.level < 0
        || (dimension < 2 && key.logical_x2 != 0)
        || (dimension < 3 && key.logical_x3 != 0))
        throw std::invalid_argument("invalid AMR logical block key");
}

constexpr bool rule_matches(AmrPlanKind kind, RefinementRule rule) noexcept
{
    switch (kind) {
    case AmrPlanKind::CoarseFineTransfer:
        return rule == RefinementRule::CoarseGhostInjection
            || rule == RefinementRule::FineGhostAverage;
    case AmrPlanKind::Prolongation:
        return rule == RefinementRule::ConservativeMinmodProlongation;
    case AmrPlanKind::Restriction:
        return rule == RefinementRule::VolumeRestriction;
    case AmrPlanKind::FluxRegistration:
        return rule == RefinementRule::FineFluxContribution
            || rule == RefinementRule::CoarseFluxContribution;
    case AmrPlanKind::Reflux:
        return rule == RefinementRule::RefluxCorrection;
    }
    return false;
}

inline auto canonical_key(const AmrTransferOperation& operation)
{
    return std::tuple{
        axis_value(operation.axis), side_value(operation.side),
        operation.destination.logical, operation.destination.handle.uid,
        operation.source.logical, operation.source.handle.uid,
        static_cast<int>(operation.field), operation.component,
        operation.destination_box, operation.source_box};
}

} // namespace amr_plan_detail

inline bool canonical_operation_less(const AmrTransferOperation& lhs,
                                     const AmrTransferOperation& rhs)
{
    return amr_plan_detail::canonical_key(lhs)
        < amr_plan_detail::canonical_key(rhs);
}

template <AmrPlanKind Kind>
std::uint64_t compute_amr_plan_fingerprint(const BasicAmrPlan<Kind>& plan)
    noexcept
{
    amr_plan_detail::Fingerprint hash;
    hash.u32(1); // AMR operation plan contract version.
    hash.byte(static_cast<std::uint8_t>(Kind));
    hash.i32(plan.dimension);
    hash.u64(plan.scope.transaction_id);
    hash.u64(plan.scope.from_epoch.value);
    hash.u64(plan.scope.to_epoch.value);
    hash.u64(static_cast<std::uint64_t>(plan.operations.size()));
    for (const auto& operation : plan.operations) {
        hash.u64(operation.ordinal);
        amr_plan_detail::hash_endpoint(hash, operation.source);
        amr_plan_detail::hash_endpoint(hash, operation.destination);
        amr_plan_detail::hash_box(hash, operation.source_box);
        amr_plan_detail::hash_box(hash, operation.destination_box);
        hash.byte(static_cast<std::uint8_t>(operation.axis));
        hash.byte(static_cast<std::uint8_t>(operation.side));
        hash.byte(static_cast<std::uint8_t>(operation.field));
        hash.i32(operation.component);
        hash.byte(static_cast<std::uint8_t>(operation.rule));
        hash.u64(std::bit_cast<std::uint64_t>(operation.weight));
        hash.u64(std::bit_cast<std::uint64_t>(operation.sign));
    }
    return hash.value();
}

template <AmrPlanKind Kind>
void validate_amr_plan(const BasicAmrPlan<Kind>& plan)
{
    if (plan.dimension < 1 || plan.dimension > 3)
        throw std::invalid_argument("AMR plan dimension must be in [1,3]");

    const bool migration = is_migration_plan(Kind);
    if (migration) {
        if (plan.scope.transaction_id == 0
            || !is_valid(plan.scope.from_epoch)
            || !is_valid(plan.scope.to_epoch)
            || plan.scope.from_epoch == plan.scope.to_epoch)
            throw std::invalid_argument("invalid AMR migration scope");
    } else if (plan.scope.transaction_id != 0
               || !is_valid(plan.scope.from_epoch)
               || plan.scope.from_epoch != plan.scope.to_epoch) {
        throw std::invalid_argument("invalid same-epoch AMR plan scope");
    }

    for (std::size_t index = 0; index < plan.operations.size(); ++index) {
        const auto& operation = plan.operations[index];
        if (operation.ordinal != index)
            throw std::invalid_argument("AMR operation ordinals are not contiguous");
        if (index != 0) {
            const auto& previous = plan.operations[index - 1];
            if (canonical_operation_less(operation, previous))
                throw std::invalid_argument("AMR operations are not canonical");
            if (!canonical_operation_less(previous, operation)
                && !canonical_operation_less(operation, previous))
                throw std::invalid_argument("duplicate AMR logical operation");
        }

        const int axis = axis_value(operation.axis);
        const int side = side_value(operation.side);
        if (axis < 0 || axis >= plan.dimension || side < 0 || side > 1)
            throw std::invalid_argument("invalid AMR operation axis or side");
        amr_plan_detail::validate_logical_key(
            operation.source.logical, plan.dimension);
        amr_plan_detail::validate_logical_key(
            operation.destination.logical, plan.dimension);
        (void)amr_plan_detail::checked_box_cells(operation.source_box);
        (void)amr_plan_detail::checked_box_cells(operation.destination_box);
        if (!is_valid(operation.source.handle)
            || !is_valid(operation.destination.handle))
            throw std::invalid_argument("invalid AMR operation handle");

        if (migration) {
            if (operation.source.handle.epoch != plan.scope.from_epoch
                || operation.destination.handle.epoch != plan.scope.to_epoch)
                throw std::invalid_argument("AMR migration endpoint epoch mismatch");
        } else if (operation.source.handle.epoch != plan.scope.from_epoch
                   || operation.destination.handle.epoch
                       != plan.scope.from_epoch) {
            throw std::invalid_argument("same-epoch AMR endpoint mismatch");
        }

        if (static_cast<std::uint8_t>(operation.field)
            > static_cast<std::uint8_t>(AmrField::Species))
            throw std::invalid_argument("invalid AMR field");
        if (operation.field == AmrField::Species) {
            if (operation.component < 0)
                throw std::invalid_argument("invalid AMR species component");
        } else if (operation.component != -1) {
            throw std::invalid_argument("non-species AMR field has component");
        }
        if (!amr_plan_detail::rule_matches(Kind, operation.rule))
            throw std::invalid_argument("AMR refinement rule does not match plan");
        if (!amr_plan_detail::is_finite_binary64(operation.weight)
            || operation.weight < 0.0)
            throw std::invalid_argument("invalid AMR operation weight");
        if (!amr_plan_detail::is_finite_binary64(operation.sign)
            || (operation.sign != -1.0 && operation.sign != 1.0))
            throw std::invalid_argument("invalid AMR operation sign");

        const int source_level = operation.source.logical.level;
        const int destination_level = operation.destination.logical.level;
        const bool level_ok = [&] {
            switch (operation.rule) {
            case RefinementRule::CoarseGhostInjection:
            case RefinementRule::ConservativeMinmodProlongation:
                return source_level + 1 == destination_level;
            case RefinementRule::FineGhostAverage:
            case RefinementRule::VolumeRestriction:
                return source_level == destination_level + 1;
            case RefinementRule::FineFluxContribution:
                return source_level == destination_level + 1;
            case RefinementRule::CoarseFluxContribution:
            case RefinementRule::RefluxCorrection:
                return source_level == destination_level;
            }
            return false;
        }();
        if (!level_ok)
            throw std::invalid_argument("AMR operation level relation is invalid");
    }

    if (plan.fingerprint != compute_amr_plan_fingerprint(plan))
        throw std::invalid_argument("AMR plan fingerprint mismatch");
}

template <AmrPlanKind Kind>
void finalize_amr_plan(BasicAmrPlan<Kind>& plan)
{
    std::sort(plan.operations.begin(), plan.operations.end(),
              canonical_operation_less);
    for (std::size_t index = 0; index < plan.operations.size(); ++index)
        plan.operations[index].ordinal = index;
    plan.fingerprint = compute_amr_plan_fingerprint(plan);
    validate_amr_plan(plan);
}

static_assert(std::is_trivially_copyable_v<LogicalAmrBox>);
static_assert(std::is_trivially_copyable_v<AmrTransferOperation>);
static_assert(std::is_standard_layout_v<AmrTransferOperation>);

} // namespace amr
