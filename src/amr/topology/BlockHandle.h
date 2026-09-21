/**
 * @file BlockHandle.h
 * @brief Backend-neutral identities for blocks, topology epochs, and logical cells.
 *
 * A handle pairs a persistent block UID with the topology epoch in which it
 * is valid. Neither value is a memory-pool index or a device address. Logical
 * keys describe topology/cell locations; backend stores resolve handles to
 * their own allocation slots and must reject stale epoch bindings.
 */

#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace amr {

struct BlockUid {
    std::uint64_t value = 0;
    friend constexpr auto operator<=>(const BlockUid&, const BlockUid&) = default;
};

struct TopologyEpoch {
    std::uint64_t value = 0;
    friend constexpr auto operator<=>(const TopologyEpoch&, const TopologyEpoch&) = default;
};

struct BlockHandle {
    BlockUid uid{};
    TopologyEpoch epoch{};
    friend constexpr auto operator<=>(const BlockHandle&, const BlockHandle&) = default;
};

struct RootLogicalKey {
    std::int64_t root_i = 0;
    std::int64_t root_j = 0;
    std::int64_t root_k = 0;
    friend constexpr auto operator<=>(const RootLogicalKey&, const RootLogicalKey&) = default;
};

struct CellLogicalKey {
    RootLogicalKey root{};
    int level = 0;
    std::uint64_t morton = 0;
    int logical_i = 0;
    int logical_j = 0;
    int logical_k = 0;
    int component = 0;
    friend constexpr auto operator<=>(const CellLogicalKey&, const CellLogicalKey&) = default;
};

constexpr bool is_valid(BlockUid uid) noexcept
{
    return uid.value != 0;
}

constexpr bool is_valid(TopologyEpoch epoch) noexcept
{
    return epoch.value != 0;
}

constexpr bool is_valid(BlockHandle handle) noexcept
{
    return is_valid(handle.uid) && is_valid(handle.epoch);
}

inline std::optional<RootLogicalKey> root_logical_key_from_leaf(
    int level, std::uint32_t logical_x1,
    std::uint32_t logical_x2, std::uint32_t logical_x3) noexcept
{
    constexpr int coordinate_bits = std::numeric_limits<std::uint32_t>::digits;
    if (level < 0 || level >= coordinate_bits) return std::nullopt;
    return RootLogicalKey{
        static_cast<std::int64_t>(logical_x1 >> level),
        static_cast<std::int64_t>(logical_x2 >> level),
        static_cast<std::int64_t>(logical_x3 >> level)};
}

class BlockIdentityAuthority {
public:
    explicit BlockIdentityAuthority(
        std::uint64_t next_uid = 1,
        std::uint64_t current_epoch = 1)
        : next_uid_(next_uid), current_epoch_(current_epoch)
    {
        if (next_uid_ == 0 || current_epoch_ == 0) {
            throw std::invalid_argument("block identity counters must be nonzero");
        }
    }

    BlockUid issue_uid()
    {
        if (next_uid_ == 0) {
            throw std::overflow_error("BlockUid counter exhausted");
        }
        const BlockUid issued{next_uid_};
        if (next_uid_ == std::numeric_limits<std::uint64_t>::max()) {
            next_uid_ = 0;
        } else {
            ++next_uid_;
        }
        return issued;
    }

    TopologyEpoch current_epoch() const noexcept
    {
        return TopologyEpoch{current_epoch_};
    }

    TopologyEpoch commit_topology()
    {
        if (current_epoch_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("TopologyEpoch counter exhausted");
        }
        ++current_epoch_;
        return TopologyEpoch{current_epoch_};
    }

    BlockHandle bind(BlockUid uid) const
    {
        if (!is_valid(uid)) {
            throw std::invalid_argument("cannot bind an invalid BlockUid");
        }
        return BlockHandle{uid, current_epoch()};
    }

    bool accepts(BlockHandle handle) const noexcept
    {
        return is_valid(handle) && handle.epoch.value == current_epoch_;
    }

private:
    std::uint64_t next_uid_;
    std::uint64_t current_epoch_;
};

} // namespace amr
