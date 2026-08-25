/**
 * @file TopologyIdentityRegistry.h
 * @brief Host-only reconciliation of stable logical blocks to BlockHandle.
 */

#pragma once

#include "amr/BlockHandle.h"

#include <array>
#include <compare>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace arch::topology {

struct LogicalBlockIdentity {
    int dimension = 0;
    int level = 0;
    std::uint32_t logical_x1 = 0;
    std::uint32_t logical_x2 = 0;
    std::uint32_t logical_x3 = 0;
    friend constexpr auto operator<=>(const LogicalBlockIdentity&,
                                      const LogicalBlockIdentity&) = default;
};

struct TopologyDomainBounds {
    int dimension = 0;
    std::array<std::uint32_t, 3> root_extents{};
    int max_level = 0;
};

struct TopologyObservation {
    int pool_index = -1;
    LogicalBlockIdentity identity{};
};

struct TopologyReconciliation {
    bool initial_adoption = false;
    bool topology_changed = false;
    amr::TopologyEpoch epoch{};
    std::vector<amr::BlockHandle> handles_in_observation_order;
    std::vector<amr::BlockUid> retired;
    std::vector<amr::BlockUid> added;
};

class TopologyIdentityRegistry {
public:
    class Candidate {
    public:
        Candidate(const Candidate&) = delete;
        Candidate& operator=(const Candidate&) = delete;
        Candidate(Candidate&& other) noexcept
            : base_generation_(other.base_generation_),
              base_adopted_(other.base_adopted_),
              consumed_(other.consumed_), valid_(other.valid_),
              authority_(std::move(other.authority_)),
              identities_(std::move(other.identities_)),
              handles_(std::move(other.handles_)),
              lowering_(std::move(other.lowering_)),
              retired_(std::move(other.retired_)),
              result_(std::move(other.result_))
        {
            other.poison_moved_from();
        }
        Candidate& operator=(Candidate&&) = delete;

        const TopologyReconciliation& reconciliation() const
        {
            if (!valid_ || consumed_)
                throw std::logic_error(
                    "topology candidate is no longer inspectable");
            return result_;
        }

    private:
        friend class TopologyIdentityRegistry;

        Candidate(
            std::uint64_t base_generation, bool base_adopted,
            amr::BlockIdentityAuthority authority,
            std::map<LogicalBlockIdentity, amr::BlockUid> identities,
            std::map<LogicalBlockIdentity, amr::BlockHandle> handles,
            std::map<int, amr::BlockHandle> lowering,
            std::set<amr::BlockUid> retired,
            TopologyReconciliation result)
            : base_generation_(base_generation),
              base_adopted_(base_adopted), authority_(std::move(authority)),
              identities_(std::move(identities)),
              handles_(std::move(handles)), lowering_(std::move(lowering)),
              retired_(std::move(retired)), result_(std::move(result))
        {
        }

        void poison_moved_from() noexcept
        {
            base_generation_ = std::numeric_limits<std::uint64_t>::max();
            base_adopted_ = false;
            consumed_ = true;
            valid_ = false;
        }

        std::uint64_t base_generation_ = 0;
        bool base_adopted_ = false;
        bool consumed_ = false;
        bool valid_ = true;
        amr::BlockIdentityAuthority authority_{};
        std::map<LogicalBlockIdentity, amr::BlockUid> identities_;
        std::map<LogicalBlockIdentity, amr::BlockHandle> handles_;
        std::map<int, amr::BlockHandle> lowering_;
        std::set<amr::BlockUid> retired_;
        TopologyReconciliation result_;
    };

    explicit TopologyIdentityRegistry(TopologyDomainBounds bounds)
        : bounds_(bounds)
    {
        validate_bounds(bounds_);
    }

    Candidate stage_adoption(
        const std::vector<TopologyObservation>& observations) const
    {
        if (adopted_)
            throw std::logic_error("topology registry is already adopted");
        const Snapshot snapshot = validate_snapshot(observations);

        amr::BlockIdentityAuthority staged_authority = authority_;
        std::map<LogicalBlockIdentity, amr::BlockUid> identities;
        std::vector<amr::BlockUid> added;
        for (const auto& [identity, pool_index] : snapshot.identity_to_pool) {
            (void)pool_index;
            const amr::BlockUid uid = staged_authority.issue_uid();
            identities.emplace(identity, uid);
            added.push_back(uid);
        }

        auto handles = build_handles(identities, staged_authority);
        auto lowering = build_pool_lowering(snapshot, handles);
        TopologyReconciliation result = make_result(
            snapshot, true, false, {}, std::move(added), staged_authority,
            handles);
        return Candidate{generation_, false, std::move(staged_authority),
                         std::move(identities), std::move(handles),
                         std::move(lowering), retired_uids_,
                         std::move(result)};
    }

    Candidate stage_reconciliation(
        const std::vector<TopologyObservation>& observations) const
    {
        require_adopted();
        const Snapshot snapshot = validate_snapshot(observations);
        const bool changed = logical_key_set_changed(snapshot);
        if (!changed) {
            auto lowering = build_pool_lowering(snapshot, active_handles_);
            TopologyReconciliation result = make_result(
                snapshot, false, false, {}, {}, authority_, active_handles_);
            return Candidate{generation_, true, authority_, identity_to_uid_,
                             active_handles_, std::move(lowering),
                             retired_uids_, std::move(result)};
        }

        amr::BlockIdentityAuthority staged_authority = authority_;
        (void)staged_authority.commit_topology();
        std::map<LogicalBlockIdentity, amr::BlockUid> next_identities;
        std::vector<amr::BlockUid> added;
        for (const auto& [identity, pool_index] : snapshot.identity_to_pool) {
            (void)pool_index;
            const auto survivor = identity_to_uid_.find(identity);
            if (survivor != identity_to_uid_.end()) {
                next_identities.emplace(identity, survivor->second);
            } else {
                const amr::BlockUid uid = staged_authority.issue_uid();
                next_identities.emplace(identity, uid);
                added.push_back(uid);
            }
        }

        std::set<amr::BlockUid> next_retired = retired_uids_;
        std::vector<amr::BlockUid> retired;
        for (const auto& [identity, uid] : identity_to_uid_) {
            if (!next_identities.contains(identity)) {
                next_retired.insert(uid);
                retired.push_back(uid);
            }
        }

        auto handles = build_handles(next_identities, staged_authority);
        auto lowering = build_pool_lowering(snapshot, handles);
        TopologyReconciliation result = make_result(
            snapshot, false, true, std::move(retired), std::move(added),
            staged_authority, handles);
        return Candidate{generation_, true, std::move(staged_authority),
                         std::move(next_identities), std::move(handles),
                         std::move(lowering), std::move(next_retired),
                         std::move(result)};
    }

    void commit(Candidate&& candidate)
    {
        PreparedTransaction prepared = prepare_candidate(candidate);
        CommitGuard guard(*this);
        publish_prevalidated(std::move(prepared));
    }

    template <typename Finalizer>
    TopologyReconciliation commit_after_success(
        Candidate&& candidate, Finalizer&& finalizer)
    {
        PreparedTransaction prepared = prepare_candidate(candidate);
        TopologyReconciliation result = std::move(prepared.result);
        static_assert(std::is_nothrow_move_constructible_v<
                      TopologyReconciliation>);

        CommitGuard guard(*this);
        std::forward<Finalizer>(finalizer)(std::as_const(result));
        publish_prevalidated(std::move(prepared));
        return result;
    }

    void validate_committed_snapshot(
        const std::vector<TopologyObservation>& observations) const
    {
        require_adopted();
        const Snapshot snapshot = validate_snapshot(observations);
        if (snapshot.identity_to_pool.size() != identity_to_uid_.size()
            || snapshot.pool_to_identity.size() != pool_lowering_.size()
            || active_handles_.size() != identity_to_uid_.size()) {
            throw std::invalid_argument(
                "committed topology snapshot is incomplete");
        }

        std::set<amr::BlockHandle> unique_handles;
        for (const auto& [identity, pool_index] : snapshot.identity_to_pool) {
            const auto active = active_handles_.find(identity);
            const auto lowering = pool_lowering_.find(pool_index);
            if (active == active_handles_.end()
                || lowering == pool_lowering_.end()
                || lowering->second != active->second
                || !unique_handles.insert(active->second).second) {
                throw std::invalid_argument(
                    "committed topology snapshot is not bijective");
            }
        }
        for (const auto& [identity, uid] : identity_to_uid_) {
            const auto incoming = snapshot.identity_to_pool.find(identity);
            const auto active = active_handles_.find(identity);
            if (incoming == snapshot.identity_to_pool.end()
                || active == active_handles_.end()
                || active->second.uid != uid) {
                throw std::invalid_argument(
                    "committed topology snapshot has missing identity");
            }
        }
    }

    amr::TopologyEpoch epoch() const
    {
        require_adopted();
        return authority_.current_epoch();
    }

    amr::BlockHandle handle_for(LogicalBlockIdentity identity) const
    {
        require_adopted();
        const auto found = active_handles_.find(identity);
        if (found == active_handles_.end())
            throw std::invalid_argument("logical block identity is not active");
        return found->second;
    }

    amr::BlockHandle handle_for_pool(int pool_index) const
    {
        require_adopted();
        const auto found = pool_lowering_.find(pool_index);
        if (found == pool_lowering_.end())
            throw std::invalid_argument("pool index is not in current lowering");
        return found->second;
    }

    bool is_retired(amr::BlockUid uid) const noexcept
    {
        return retired_uids_.contains(uid);
    }

private:
    struct PreparedTransaction {
        amr::BlockIdentityAuthority authority;
        std::map<LogicalBlockIdentity, amr::BlockUid> identities;
        std::map<LogicalBlockIdentity, amr::BlockHandle> handles;
        std::map<int, amr::BlockHandle> lowering;
        std::set<amr::BlockUid> retired;
        TopologyReconciliation result;
    };

    class CommitGuard {
    public:
        explicit CommitGuard(TopologyIdentityRegistry& registry) noexcept
            : registry_(registry)
        {
            registry_.commit_in_progress_ = true;
        }

        CommitGuard(const CommitGuard&) = delete;
        CommitGuard& operator=(const CommitGuard&) = delete;

        ~CommitGuard()
        {
            registry_.commit_in_progress_ = false;
        }

    private:
        TopologyIdentityRegistry& registry_;
    };

    struct Snapshot {
        std::map<LogicalBlockIdentity, int> identity_to_pool;
        std::map<int, LogicalBlockIdentity> pool_to_identity;
        std::vector<LogicalBlockIdentity> observation_order;
    };

    void prevalidate_candidate(const Candidate& candidate) const
    {
        if (commit_in_progress_)
            throw std::logic_error("topology commit is already in progress");
        if (!candidate.valid_)
            throw std::logic_error("topology candidate was moved from");
        if (candidate.consumed_)
            throw std::logic_error("topology candidate is already consumed");
        if (candidate.base_generation_ != generation_
            || candidate.base_adopted_ != adopted_) {
            throw std::logic_error("topology candidate is stale");
        }
        if (generation_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error(
                "topology candidate generation exhausted");
    }

    PreparedTransaction prepare_candidate(Candidate& candidate)
    {
        prevalidate_candidate(candidate);

        TopologyReconciliation result = candidate.result_;
        PreparedTransaction prepared{
            std::move(candidate.authority_),
            std::move(candidate.identities_),
            std::move(candidate.handles_),
            std::move(candidate.lowering_),
            std::move(candidate.retired_),
            std::move(result)};
        candidate.consumed_ = true;
        return prepared;
    }

    void publish_prevalidated(PreparedTransaction&& prepared) noexcept
    {
        static_assert(noexcept(std::swap(authority_, prepared.authority)));
        static_assert(noexcept(identity_to_uid_.swap(prepared.identities)));
        static_assert(noexcept(active_handles_.swap(prepared.handles)));
        static_assert(noexcept(pool_lowering_.swap(prepared.lowering)));
        static_assert(noexcept(retired_uids_.swap(prepared.retired)));

        using std::swap;
        swap(authority_, prepared.authority);
        identity_to_uid_.swap(prepared.identities);
        active_handles_.swap(prepared.handles);
        pool_lowering_.swap(prepared.lowering);
        retired_uids_.swap(prepared.retired);
        adopted_ = true;
        ++generation_;
    }

    static void validate_bounds(const TopologyDomainBounds& bounds)
    {
        if (bounds.dimension < 1 || bounds.dimension > 3)
            throw std::invalid_argument("topology dimension must be 1, 2, or 3");
        if (bounds.max_level < 0
            || bounds.max_level >= std::numeric_limits<std::uint32_t>::digits) {
            throw std::invalid_argument("topology maximum level is invalid");
        }
        for (int axis = 0; axis < 3; ++axis) {
            const bool active = axis < bounds.dimension;
            if (active && bounds.root_extents[axis] == 0)
                throw std::invalid_argument("active root extent must be positive");
            if (!active && bounds.root_extents[axis] != 1)
                throw std::invalid_argument(
                    "inactive root extent must be canonical one");
            if (bounds.root_extents[axis]
                > (std::numeric_limits<std::uint32_t>::max()
                   >> bounds.max_level)) {
                throw std::invalid_argument(
                    "level-scaled root extent exceeds coordinate range");
            }
        }
    }

    void validate_identity(const LogicalBlockIdentity& identity) const
    {
        if (identity.dimension != bounds_.dimension)
            throw std::invalid_argument("logical block dimension mismatch");
        if (identity.level < 0 || identity.level > bounds_.max_level)
            throw std::invalid_argument("logical block level is out of bounds");

        const std::array<std::uint32_t, 3> coordinates{
            identity.logical_x1, identity.logical_x2, identity.logical_x3};
        for (int axis = 0; axis < 3; ++axis) {
            if (axis >= bounds_.dimension) {
                if (coordinates[axis] != 0)
                    throw std::invalid_argument(
                        "inactive logical coordinate must be zero");
                continue;
            }
            const std::uint64_t extent =
                static_cast<std::uint64_t>(bounds_.root_extents[axis])
                << identity.level;
            if (coordinates[axis] >= extent)
                throw std::invalid_argument(
                    "global logical coordinate is out of bounds");
        }
    }

    Snapshot validate_snapshot(
        const std::vector<TopologyObservation>& observations) const
    {
        if (observations.empty())
            throw std::invalid_argument("topology snapshot must not be empty");

        Snapshot snapshot;
        snapshot.observation_order.reserve(observations.size());
        for (const TopologyObservation& observation : observations) {
            if (observation.pool_index < 0)
                throw std::invalid_argument("active pool index must be nonnegative");
            validate_identity(observation.identity);
            if (!snapshot.identity_to_pool
                     .emplace(observation.identity, observation.pool_index)
                     .second) {
                throw std::invalid_argument(
                    "topology snapshot has duplicate logical identity");
            }
            if (!snapshot.pool_to_identity
                     .emplace(observation.pool_index, observation.identity)
                     .second) {
                throw std::invalid_argument(
                    "topology snapshot has duplicate pool index");
            }
            snapshot.observation_order.push_back(observation.identity);
        }
        return snapshot;
    }

    bool logical_key_set_changed(const Snapshot& snapshot) const
    {
        if (snapshot.identity_to_pool.size() != identity_to_uid_.size())
            return true;
        auto current = identity_to_uid_.begin();
        auto incoming = snapshot.identity_to_pool.begin();
        for (; current != identity_to_uid_.end(); ++current, ++incoming) {
            if (current->first != incoming->first) return true;
        }
        return false;
    }

    static std::map<LogicalBlockIdentity, amr::BlockHandle> build_handles(
        const std::map<LogicalBlockIdentity, amr::BlockUid>& identities,
        const amr::BlockIdentityAuthority& authority)
    {
        std::map<LogicalBlockIdentity, amr::BlockHandle> handles;
        for (const auto& [identity, uid] : identities)
            handles.emplace(identity, authority.bind(uid));
        return handles;
    }

    static std::map<int, amr::BlockHandle> build_pool_lowering(
        const Snapshot& snapshot,
        const std::map<LogicalBlockIdentity, amr::BlockHandle>& handles)
    {
        std::map<int, amr::BlockHandle> lowering;
        for (const auto& [pool_index, identity] : snapshot.pool_to_identity)
            lowering.emplace(pool_index, handles.at(identity));
        return lowering;
    }

    static TopologyReconciliation make_result(
        const Snapshot& snapshot, bool initial, bool changed,
        std::vector<amr::BlockUid> retired,
        std::vector<amr::BlockUid> added,
        const amr::BlockIdentityAuthority& authority,
        const std::map<LogicalBlockIdentity, amr::BlockHandle>& handles)
    {
        TopologyReconciliation result;
        result.initial_adoption = initial;
        result.topology_changed = changed;
        result.epoch = authority.current_epoch();
        result.retired = std::move(retired);
        result.added = std::move(added);
        result.handles_in_observation_order.reserve(
            snapshot.observation_order.size());
        for (const LogicalBlockIdentity& identity : snapshot.observation_order)
            result.handles_in_observation_order.push_back(
                handles.at(identity));
        return result;
    }

    void require_adopted() const
    {
        if (!adopted_)
            throw std::logic_error("topology registry has not been adopted");
    }

    TopologyDomainBounds bounds_;
    amr::BlockIdentityAuthority authority_;
    bool adopted_ = false;
    std::map<LogicalBlockIdentity, amr::BlockUid> identity_to_uid_;
    std::map<LogicalBlockIdentity, amr::BlockHandle> active_handles_;
    std::map<int, amr::BlockHandle> pool_lowering_;
    std::set<amr::BlockUid> retired_uids_;
    std::uint64_t generation_ = 0;
    bool commit_in_progress_ = false;
};

} // namespace arch::topology
