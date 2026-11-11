/**
 * @file TopologyIdentityRegistry.h
 * @brief Reconcile stable logical block identities with topology handles.
 *
 * Host-side transactions assign generations to surviving and replacement
 * blocks before publication. Tree decisions and conservative field migration
 * remain with the AMR authorities and their backend consumers.
 * Workflow:
 * 1. Receive a resolved plan and active AMR topology.
 * 2. Manage backend residency and stable state generations.
 * 3. Expose stage data only through checked runtime leases.
 */

#pragma once

#include "amr/topology/BlockHandle.h"

#include <array>
#include <compare>
#include <cstdint>
#include <limits>
#include <exception>
#include <optional>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace arch::topology {

using LogicalBlockIdentity = amr::LogicalBlockKey;

struct TopologyDomainBounds {
    int dimension = 0;
    std::array<std::uint32_t, 3> root_extents{};
    int max_level = 0;
};

struct TopologyObservation {
    int pool_index = -1;
    LogicalBlockIdentity identity{};
    int owner_rank = 0; // Remote observations have pool_index == -1.
};

// All ranks call at the same topology boundary. Only logical identities and
// ownership cross the wire; pool indices and field storage remain local.
std::vector<TopologyObservation> collect_topology_observations(
    const std::vector<TopologyObservation>& local, int rank, int rank_count);

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
            : owner_(other.owner_), base_generation_(other.base_generation_),
              base_adopted_(other.base_adopted_),
              consumed_(other.consumed_), valid_(other.valid_),
              authority_(std::move(other.authority_)),
              identities_(std::move(other.identities_)),
              handles_(std::move(other.handles_)),
              lowering_(std::move(other.lowering_)),
              owners_(std::move(other.owners_)),
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

        std::vector<amr::BlockTopology> blocks() const {
            (void)reconciliation(); // Reject moved/consumed candidates.
            return describe_blocks(handles_, owners_, lowering_);
        }

    private:
        friend class TopologyIdentityRegistry;

        Candidate(
            const TopologyIdentityRegistry* owner,
            std::uint64_t base_generation, bool base_adopted,
            amr::BlockIdentityAuthority authority,
            std::map<LogicalBlockIdentity, amr::BlockUid> identities,
            std::map<LogicalBlockIdentity, amr::BlockHandle> handles,
            std::map<int, amr::BlockHandle> lowering,
            std::map<LogicalBlockIdentity, int> owners,
            std::set<amr::BlockUid> retired,
            TopologyReconciliation result)
            : owner_(owner), base_generation_(base_generation),
              base_adopted_(base_adopted), authority_(std::move(authority)),
              identities_(std::move(identities)),
              handles_(std::move(handles)), lowering_(std::move(lowering)),
              owners_(std::move(owners)),
              retired_(std::move(retired)), result_(std::move(result))
        {
        }

        void poison_moved_from() noexcept
        {
            owner_ = nullptr;
            base_generation_ = std::numeric_limits<std::uint64_t>::max();
            base_adopted_ = false;
            consumed_ = true;
            valid_ = false;
        }

        const TopologyIdentityRegistry* owner_ = nullptr;
        std::uint64_t base_generation_ = 0;
        bool base_adopted_ = false;
        bool consumed_ = false;
        bool valid_ = true;
        amr::BlockIdentityAuthority authority_{};
        std::map<LogicalBlockIdentity, amr::BlockUid> identities_;
        std::map<LogicalBlockIdentity, amr::BlockHandle> handles_;
        std::map<int, amr::BlockHandle> lowering_;
        std::map<LogicalBlockIdentity, int> owners_;
        std::set<amr::BlockUid> retired_;
        TopologyReconciliation result_;
    };

    explicit TopologyIdentityRegistry(TopologyDomainBounds bounds,
                                      int rank = 0, int rank_count = 1)
        : bounds_(bounds), rank_(rank), rank_count_(rank_count)
    {
        validate_bounds(bounds_);
        if (rank_count < 1 || rank < 0 || rank >= rank_count)
            throw std::invalid_argument("invalid topology rank");
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
        for (const auto& [identity, owner_rank] : snapshot.owners) {
            (void)owner_rank;
            const amr::BlockUid uid = staged_authority.issue_uid();
            identities.emplace(identity, uid);
            added.push_back(uid);
        }

        auto handles = build_handles(identities, staged_authority);
        auto lowering = build_pool_lowering(snapshot, handles);
        TopologyReconciliation result = make_result(
            snapshot, true, false, {}, std::move(added), staged_authority,
            handles);
        return Candidate{this, generation_, false, std::move(staged_authority),
                         std::move(identities), std::move(handles),
                         std::move(lowering), snapshot.owners, retired_uids_,
                         std::move(result)};
    }

    Candidate stage_reconciliation(
        const std::vector<TopologyObservation>& observations) const
    {
        require_adopted();
        const Snapshot snapshot = validate_snapshot(observations);
        const bool changed = logical_key_set_changed(snapshot)
            || snapshot.owners != owners_;
        if (!changed) {
            auto lowering = build_pool_lowering(snapshot, active_handles_);
            TopologyReconciliation result = make_result(
                snapshot, false, false, {}, {}, authority_, active_handles_);
            return Candidate{this, generation_, true, authority_, identity_to_uid_,
                             active_handles_, std::move(lowering),
                             snapshot.owners, retired_uids_, std::move(result)};
        }

        amr::BlockIdentityAuthority staged_authority = authority_;
        (void)staged_authority.commit_topology();
        std::map<LogicalBlockIdentity, amr::BlockUid> next_identities;
        std::vector<amr::BlockUid> added;
        for (const auto& [identity, owner_rank] : snapshot.owners) {
            (void)owner_rank;
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
        return Candidate{this, generation_, true, std::move(staged_authority),
                         std::move(next_identities), std::move(handles),
                         std::move(lowering), snapshot.owners, std::move(next_retired),
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
        return commit_after_success(std::move(candidate),std::forward<Finalizer>(finalizer),
            [](std::exception_ptr failure) {if (failure) std::rethrow_exception(failure);});
    }

    /** Complete fallible registry staging on all participants before finalization.
     * The completion callback must propagate a local or peer preparation failure.
     */
    template <typename Finalizer, typename CompletePreparation>
    TopologyReconciliation commit_after_success(Candidate&& candidate,
        Finalizer&& finalizer, CompletePreparation&& complete_preparation)
    {
        std::optional<PreparedTransaction> staged;
        std::exception_ptr failure;
        try { staged.emplace(prepare_candidate(candidate)); }
        catch (...) { failure=std::current_exception(); }
        std::forward<CompletePreparation>(complete_preparation)(failure);
        if (failure) std::rethrow_exception(failure);
        auto& prepared=*staged;
        TopologyReconciliation result = std::move(prepared.result);
        static_assert(std::is_nothrow_move_constructible_v<
                      TopologyReconciliation>);

        CommitGuard guard(*this);
        std::forward<Finalizer>(finalizer)(std::as_const(result));
        publish_prevalidated(std::move(prepared));
        return result;
    }

    /** Validate this rank's storage without a collective or remote field views. */
    void validate_committed_snapshot(
        const std::vector<TopologyObservation>& observations) const
    {
        require_adopted();
        const Snapshot snapshot = validate_snapshot(observations, false);
        if (snapshot.identity_to_pool.size() != pool_lowering_.size()
            || active_handles_.size() != identity_to_uid_.size()
            || owners_.size() != identity_to_uid_.size()) {
            throw std::invalid_argument(
                "committed local topology snapshot is incomplete");
        }
        std::set<amr::BlockHandle> unique_handles;
        for (const auto& [identity, pool_index] : snapshot.identity_to_pool) {
            const auto active = active_handles_.find(identity);
            const auto lowering = pool_lowering_.find(pool_index);
            const auto owner = owners_.find(identity);
            if (active == active_handles_.end()
                || lowering == pool_lowering_.end()
                || owner == owners_.end() || owner->second != rank_
                || lowering->second != active->second
                || !unique_handles.insert(active->second).second) {
                throw std::invalid_argument(
                    "committed local topology snapshot is not bijective");
            }
        }
    }

    std::vector<amr::BlockTopology> blocks() const {
        require_adopted();
        return describe_blocks(active_handles_, owners_, pool_lowering_);
    }

    /** Global metadata decides the same lane even on a rank with only coarse leaves. */
    bool has_refined_blocks() const {
        require_adopted();
        for (const auto& [identity, owner] : owners_)
            if (identity.level > 0) return true;
        return false;
    }

    int owner_for(LogicalBlockIdentity identity) const
    {
        require_adopted();
        const auto found = owners_.find(identity);
        if (found == owners_.end())
            throw std::invalid_argument("logical block identity has no owner");
        return found->second;
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
        std::map<LogicalBlockIdentity, int> owners;
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
        std::map<LogicalBlockIdentity, int> owners;
        std::map<LogicalBlockIdentity, int> identity_to_pool;
        std::map<int, LogicalBlockIdentity> pool_to_identity;
        std::vector<LogicalBlockIdentity> observation_order;
    };

    void prevalidate_candidate(const Candidate& candidate) const
    {
        if (commit_in_progress_)
            throw std::logic_error("topology commit is already in progress");
        if (candidate.owner_ != this)
            throw std::logic_error("topology candidate belongs to another registry");
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
            std::move(candidate.owners_),
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
        static_assert(noexcept(owners_.swap(prepared.owners)));
        static_assert(noexcept(retired_uids_.swap(prepared.retired)));

        using std::swap;
        swap(authority_, prepared.authority);
        identity_to_uid_.swap(prepared.identities);
        active_handles_.swap(prepared.handles);
        pool_lowering_.swap(prepared.lowering);
        owners_.swap(prepared.owners);
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
        const std::vector<TopologyObservation>& observations,
        bool global = true) const
    {
        if (observations.empty() && (global || rank_count_ == 1))
            throw std::invalid_argument("topology snapshot must not be empty");
        Snapshot snapshot;
        snapshot.observation_order.reserve(observations.size());
        for (const TopologyObservation& observation : observations) {
            const bool local = observation.owner_rank == rank_;
            if (observation.owner_rank < 0 || observation.owner_rank >= rank_count_
                || (!global && !local))
                throw std::invalid_argument("topology observation has invalid owner");
            if ((local && observation.pool_index < 0)
                || (!local && observation.pool_index != -1))
                throw std::invalid_argument("topology observation has invalid local storage");
            validate_identity(observation.identity);
            if (!snapshot.owners.emplace(observation.identity, observation.owner_rank).second)
                throw std::invalid_argument("topology snapshot has duplicate logical identity");
            if (!local) continue;
            snapshot.identity_to_pool.emplace(observation.identity, observation.pool_index);
            if (!snapshot.pool_to_identity
                     .emplace(observation.pool_index, observation.identity).second)
                throw std::invalid_argument("topology snapshot has duplicate pool index");
            snapshot.observation_order.push_back(observation.identity);
        }
        if (global && rank_count_ > 1) validate_global_cover(snapshot);
        return snapshot;
    }

    // Collapse complete sibling families using integer topology only.
    // A matching leaf count alone cannot rule out gaps or parent/child overlap.
    void validate_global_cover(const Snapshot& snapshot) const
    {
        std::set<LogicalBlockIdentity> leaves;
        for (const auto& [identity, owner] : snapshot.owners) {
            (void)owner;
            leaves.insert(identity);
        }
        const unsigned full_family = (1u << (1u << bounds_.dimension)) - 1u;
        for (int level = bounds_.max_level; level > 0; --level) {
            std::map<LogicalBlockIdentity, unsigned> families;
            for (auto it = leaves.begin(); it != leaves.end();) {
                if (it->level != level) { ++it; continue; }
                const auto key = *it;
                const unsigned child = (key.logical_x1 & 1u)
                    | ((key.logical_x2 & 1u) << 1u)
                    | ((key.logical_x3 & 1u) << 2u);
                const LogicalBlockIdentity parent{key.dimension, level - 1,
                    key.logical_x1 >> 1, key.logical_x2 >> 1, key.logical_x3 >> 1};
                families[parent] |= 1u << child;
                it = leaves.erase(it);
            }
            for (const auto& [parent, children] : families) {
                if (children != full_family || !leaves.insert(parent).second)
                    throw std::invalid_argument("distributed leaves overlap or leave a gap");
            }
        }
        std::size_t roots = 1;
        for (const auto extent : bounds_.root_extents) {
            if (extent > std::numeric_limits<std::size_t>::max() / roots)
                throw std::overflow_error("topology root count overflow");
            roots *= extent;
        }
        if (leaves.size() != roots)
            throw std::invalid_argument("distributed root coverage is incomplete");
    }

    bool logical_key_set_changed(const Snapshot& snapshot) const
    {
        if (snapshot.owners.size() != identity_to_uid_.size()) return true;
        auto current = identity_to_uid_.begin();
        auto incoming = snapshot.owners.begin();
        for (; current != identity_to_uid_.end(); ++current, ++incoming) {
            if (current->first != incoming->first) return true;
        }
        return false;
    }

    static std::vector<amr::BlockTopology>
    describe_blocks(const std::map<LogicalBlockIdentity, amr::BlockHandle>& handles,
                    const std::map<LogicalBlockIdentity, int>& owners,
                    const std::map<int, amr::BlockHandle>& lowering) {
        std::map<amr::BlockHandle, int> local_slots;
        for (const auto& [slot, handle] : lowering)
            local_slots.emplace(handle, slot);
        std::vector<amr::BlockTopology> result;
        result.reserve(handles.size());
        for (const auto& [logical, handle] : handles) {
            const auto local = local_slots.find(handle);
            result.push_back({logical, handle, owners.at(logical),
                              local == local_slots.end() ? -1 : local->second});
        }
        return result;
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
    int rank_ = 0, rank_count_ = 1;
    amr::BlockIdentityAuthority authority_;
    bool adopted_ = false;
    std::map<LogicalBlockIdentity, amr::BlockUid> identity_to_uid_;
    std::map<LogicalBlockIdentity, amr::BlockHandle> active_handles_;
    std::map<int, amr::BlockHandle> pool_lowering_;
    std::map<LogicalBlockIdentity, int> owners_;
    std::set<amr::BlockUid> retired_uids_;
    std::uint64_t generation_ = 0;
    bool commit_in_progress_ = false;
};

} // namespace arch::topology
