/**
 * @file ReductionSpec.h
 * @brief Backend-neutral deterministic reduction semantics.
 */
#pragma once

#include "amr/BlockHandle.h"
#include "core/ArchPortability.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace arch::reduction
{
enum class ReductionKind : std::uint8_t { Min, Max, Sum };
enum class ReductionNanPolicy : std::uint8_t { Ignore, Error };
enum class ReductionEmptyPolicy : std::uint8_t { Error };
enum class ReductionTiePolicy : std::uint8_t { LowestCellLogicalKey };
enum class ReductionAccumulator : std::uint8_t { Binary64 };
enum class ReductionOrder : std::uint8_t {
    OrderIndependent, CellLogicalKeyAscending
};
enum class ReductionDeterminism : std::uint8_t {
    DeterministicValueAndTie
};
enum class ReductionStatus : std::uint8_t {
    Ok, Empty, NanRejected, InfiniteRejected, DuplicateLogicalKey,
    InvalidKeyOrder, PartialStateRejected
};

struct ReductionSpec
{
    ReductionKind kind;
    ReductionNanPolicy nan;
    ReductionEmptyPolicy empty;
    ReductionTiePolicy tie;
    ReductionAccumulator accumulator;
    ReductionOrder order;
    ReductionDeterminism determinism;
    double identity;
};

struct ReductionCandidate
{
    double value;
    amr::CellLogicalKey key;
    bool active;
};

struct ReductionState
{
    double value;
    amr::CellLogicalKey key;
    std::uint64_t accepted_count;
    ReductionStatus status;
};

struct ReductionResult
{
    double value;
    amr::CellLogicalKey key;
    std::uint64_t accepted_count;
    ReductionStatus status;
};

constexpr ReductionSpec minimum_spec(double identity) noexcept
{
    return {
        ReductionKind::Min,
        ReductionNanPolicy::Ignore,
        ReductionEmptyPolicy::Error,
        ReductionTiePolicy::LowestCellLogicalKey,
        ReductionAccumulator::Binary64,
        ReductionOrder::OrderIndependent,
        ReductionDeterminism::DeterministicValueAndTie,
        identity};
}

constexpr ReductionSpec maximum_spec(double identity) noexcept
{
    return {
        ReductionKind::Max,
        ReductionNanPolicy::Ignore,
        ReductionEmptyPolicy::Error,
        ReductionTiePolicy::LowestCellLogicalKey,
        ReductionAccumulator::Binary64,
        ReductionOrder::OrderIndependent,
        ReductionDeterminism::DeterministicValueAndTie,
        identity};
}

constexpr ReductionSpec conservation_sum_spec() noexcept
{
    return {
        ReductionKind::Sum,
        ReductionNanPolicy::Error,
        ReductionEmptyPolicy::Error,
        ReductionTiePolicy::LowestCellLogicalKey,
        ReductionAccumulator::Binary64,
        ReductionOrder::CellLogicalKeyAscending,
        ReductionDeterminism::DeterministicValueAndTie,
        +0.0};
}

ARCH_INLINE bool cell_logical_key_less(
    const amr::CellLogicalKey& lhs,
    const amr::CellLogicalKey& rhs) noexcept
{
    if (lhs.root.root_i != rhs.root.root_i)
        return lhs.root.root_i < rhs.root.root_i;
    if (lhs.root.root_j != rhs.root.root_j)
        return lhs.root.root_j < rhs.root.root_j;
    if (lhs.root.root_k != rhs.root.root_k)
        return lhs.root.root_k < rhs.root.root_k;
    if (lhs.level != rhs.level) return lhs.level < rhs.level;
    if (lhs.morton != rhs.morton) return lhs.morton < rhs.morton;
    if (lhs.logical_i != rhs.logical_i) return lhs.logical_i < rhs.logical_i;
    if (lhs.logical_j != rhs.logical_j) return lhs.logical_j < rhs.logical_j;
    if (lhs.logical_k != rhs.logical_k) return lhs.logical_k < rhs.logical_k;
    return lhs.component < rhs.component;
}

ARCH_INLINE bool cell_logical_key_equal(
    const amr::CellLogicalKey& lhs,
    const amr::CellLogicalKey& rhs) noexcept
{
    return !cell_logical_key_less(lhs, rhs)
        && !cell_logical_key_less(rhs, lhs);
}

ARCH_INLINE std::uint64_t reduction_double_bits(double value) noexcept
{
#if defined(__CUDA_ARCH__)
    return static_cast<std::uint64_t>(__double_as_longlong(value));
#else
    return std::bit_cast<std::uint64_t>(value);
#endif
}

ARCH_INLINE bool reduction_value_is_nan(double value) noexcept
{
    constexpr std::uint64_t exponent = 0x7ff0000000000000ULL;
    constexpr std::uint64_t mantissa = 0x000fffffffffffffULL;
    const std::uint64_t bits = reduction_double_bits(value);
    return (bits & exponent) == exponent && (bits & mantissa) != 0;
}

ARCH_INLINE bool reduction_value_is_infinite(double value) noexcept
{
    constexpr std::uint64_t magnitude = 0x7fffffffffffffffULL;
    constexpr std::uint64_t infinity = 0x7ff0000000000000ULL;
    return (reduction_double_bits(value) & magnitude) == infinity;
}

ARCH_INLINE ReductionState begin_reduction(const ReductionSpec& spec) noexcept
{
    return {spec.identity, {}, 0, ReductionStatus::Ok};
}

ARCH_INLINE void combine_candidate(
    const ReductionSpec& spec, ReductionState& state,
    const ReductionCandidate& candidate) noexcept
{
    if (state.status != ReductionStatus::Ok || !candidate.active) return;
    if (reduction_value_is_nan(candidate.value)) {
        if (spec.nan == ReductionNanPolicy::Error)
            state.status = ReductionStatus::NanRejected;
        return;
    }
    if (spec.kind == ReductionKind::Sum
        && reduction_value_is_infinite(candidate.value)) {
        state.status = ReductionStatus::InfiniteRejected;
        return;
    }

    if (state.accepted_count == 0) {
        state.value = candidate.value;
        state.key = candidate.key;
        state.accepted_count = 1;
        return;
    }

    if (spec.kind == ReductionKind::Sum) {
        state.value += candidate.value;
        if (cell_logical_key_less(candidate.key, state.key))
            state.key = candidate.key;
    } else {
        const bool improves = spec.kind == ReductionKind::Min
            ? candidate.value < state.value
            : candidate.value > state.value;
        const bool wins_tie = candidate.value == state.value
            && cell_logical_key_less(candidate.key, state.key);
        if (improves || wins_tie) {
            state.value = candidate.value;
            state.key = candidate.key;
        }
    }
    ++state.accepted_count;
}

ARCH_INLINE void combine_state(
    const ReductionSpec& spec, ReductionState& state,
    const ReductionState& candidate) noexcept
{
    if (state.status != ReductionStatus::Ok) return;
    if (spec.kind == ReductionKind::Sum) {
        state.status = ReductionStatus::PartialStateRejected;
        return;
    }
    if (candidate.status != ReductionStatus::Ok) {
        state.status = candidate.status;
        return;
    }
    if (candidate.accepted_count == 0) return;
    if (state.accepted_count == 0) {
        state = candidate;
        return;
    }

    const bool improves = spec.kind == ReductionKind::Min
        ? candidate.value < state.value
        : candidate.value > state.value;
    const bool wins_tie = candidate.value == state.value
        && cell_logical_key_less(candidate.key, state.key);
    if (improves || wins_tie) {
        state.value = candidate.value;
        state.key = candidate.key;
    }
    state.accepted_count += candidate.accepted_count;
}

ARCH_INLINE ReductionResult finalize_reduction(
    const ReductionSpec&, const ReductionState& state) noexcept
{
    ReductionResult result{
        state.value, state.key, state.accepted_count, state.status};
    if (result.status == ReductionStatus::Ok && result.accepted_count == 0)
        result.status = ReductionStatus::Empty;
    return result;
}

ARCH_INLINE ReductionResult execute_ordered_reduction(
    const ReductionSpec& spec, const ReductionCandidate* candidates,
    std::size_t count) noexcept
{
    ReductionState state = begin_reduction(spec);
    bool have_previous_key = false;
    amr::CellLogicalKey previous_key{};
    for (std::size_t i = 0; i < count; ++i) {
        if (have_previous_key
            && !cell_logical_key_less(previous_key, candidates[i].key)) {
            state = begin_reduction(spec);
            state.status = ReductionStatus::InvalidKeyOrder;
            return finalize_reduction(spec, state);
        }
        previous_key = candidates[i].key;
        have_previous_key = true;
        if (!candidates[i].active) continue;
        combine_candidate(spec, state, candidates[i]);
        if (state.status != ReductionStatus::Ok)
            return finalize_reduction(spec, state);
    }
    return finalize_reduction(spec, state);
}

inline ReductionResult execute_host_reduction(
    const ReductionSpec& spec,
    std::span<const ReductionCandidate> candidates)
{
    std::vector<ReductionCandidate> key_ordered(
        candidates.begin(), candidates.end());
    std::sort(key_ordered.begin(), key_ordered.end(),
        [](const ReductionCandidate& lhs, const ReductionCandidate& rhs) {
            return cell_logical_key_less(lhs.key, rhs.key);
        });
    return execute_ordered_reduction(
        spec, key_ordered.data(), key_ordered.size());
}
} // namespace arch::reduction
