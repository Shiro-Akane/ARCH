/**
 * @file DeviceBlockStore.h
 * @brief Ordinary-C++ identity and lifecycle contract for device block storage.
 */

#pragma once

#include "amr/AmrTransferPlans.h"
#include "driver/ComputeBackend.h"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace arch::cuda {

struct DeviceLayoutGeneration {
    std::uint64_t value = 0;
    friend constexpr auto operator<=>(const DeviceLayoutGeneration&,
                                      const DeviceLayoutGeneration&) = default;
};

constexpr bool is_valid(DeviceLayoutGeneration generation) noexcept
{
    return generation.value != 0;
}

struct DeviceBlockRecord {
    amr::BlockHandle handle{};
    backend::StorageGeneration storage{};
    DeviceLayoutGeneration layout{};

    friend constexpr auto operator<=>(const DeviceBlockRecord&,
                                      const DeviceBlockRecord&) = default;
};

class DeviceBlockStoreIndex {
public:
    explicit DeviceBlockStoreIndex(
        std::span<const DeviceBlockRecord> records)
        : records_(records.begin(), records.end())
    {
        if (records_.empty())
            throw std::invalid_argument("device block store is empty");
        const amr::TopologyEpoch epoch = records_.front().handle.epoch;
        std::set<backend::StorageGeneration> storage_generations;
        std::set<DeviceLayoutGeneration> layout_generations;
        for (std::size_t index = 0; index < records_.size(); ++index) {
            const auto& record = records_[index];
            if (!amr::is_valid(record.handle)
                || record.handle.epoch.value != epoch.value
                || !backend::is_valid(record.storage)
                || !is_valid(record.layout))
                throw std::invalid_argument("invalid device block record");
            if (!indices_.emplace(record.handle, index).second)
                throw std::invalid_argument("duplicate device BlockHandle");
            if (!storage_generations.insert(record.storage).second)
                throw std::invalid_argument(
                    "duplicate device storage generation");
            if (!layout_generations.insert(record.layout).second)
                throw std::invalid_argument(
                    "duplicate device layout generation");
        }
    }

    std::size_t size() const noexcept { return records_.size(); }

    std::span<const DeviceBlockRecord> records() const noexcept
    {
        return records_;
    }

    bool contains(backend::BackendStateAccess access) const noexcept
    {
        const auto found = indices_.find(access.block);
        return found != indices_.end()
            && records_[found->second].storage.value == access.storage.value;
    }

    std::size_t index_of(backend::BackendStateAccess access) const
    {
        const auto found = indices_.find(access.block);
        if (found == indices_.end()
            || records_[found->second].storage.value != access.storage.value)
            throw std::invalid_argument("stale device block access");
        return found->second;
    }

    const DeviceBlockRecord& record(
        backend::BackendStateAccess access) const
    {
        return records_[index_of(access)];
    }

private:
    std::vector<DeviceBlockRecord> records_;
    std::map<amr::BlockHandle, std::size_t> indices_;
};

struct DeviceArenaSlot {
    std::uint64_t value = 0;
    friend constexpr auto operator<=>(const DeviceArenaSlot&,
                                      const DeviceArenaSlot&) = default;
};

struct DeviceRetirementFence {
    std::uint64_t value = 0;
    friend constexpr auto operator<=>(const DeviceRetirementFence&,
                                      const DeviceRetirementFence&) = default;
};

enum class DeviceMigrationRole : std::uint8_t {
    ActiveOldSource,
    StagedNewDestination,
};

struct DeviceMigrationAccess {
    amr::AmrPlanScope scope{};
    backend::BackendStateAccess access{};
    DeviceMigrationRole role = DeviceMigrationRole::ActiveOldSource;
};

struct DeviceStoreEntry {
    DeviceBlockRecord record{};
    DeviceArenaSlot arena{};

    friend constexpr auto operator<=>(const DeviceStoreEntry&,
                                      const DeviceStoreEntry&) = default;
};

/**
 * @brief A proposed device identity whose storage generation was issued by
 *        the shared driver authority.
 *
 * The device store deliberately has no storage-generation issuer.  This keeps
 * BackendStateAccess identical across the scheduler, residency ledger, and
 * every backend-specific allocation table.
 */
struct DeviceStoreProposal {
    amr::BlockHandle handle{};
    backend::StorageGeneration storage{};

    friend constexpr auto operator<=>(const DeviceStoreProposal&,
                                      const DeviceStoreProposal&) = default;
};

/**
 * @brief Transactional identity/lifetime authority for device block storage.
 *
 * This ordinary-C++ class owns no CUDA allocation and performs no AMR math.
 * It only freezes which caller-issued identities are active while a CUDA
 * backend stages allocations, migrates Current state, publishes a new arena
 * namespace, and delays destruction of the old namespace until a fence.
 */
class DeviceBlockStoreLifecycle {
private:
    struct RetirementBatch {
        DeviceRetirementFence fence{};
        std::vector<DeviceStoreEntry> entries;
    };

public:
    class Candidate {
    public:
        Candidate(const Candidate&) = delete;
        Candidate& operator=(const Candidate&) = delete;
        Candidate& operator=(Candidate&&) = delete;

        Candidate(Candidate&& other) noexcept
            : owner_(std::exchange(other.owner_, nullptr)),
              scope_(other.scope_), entries_(std::move(other.entries_)),
              prepared_retirement_(std::move(other.prepared_retirement_)),
              commit_attempted_(other.commit_attempted_)
        {
            other.scope_ = {};
            other.commit_attempted_ = true;
        }

        ~Candidate()
        {
            if (owner_ != nullptr) owner_->abandon_candidate_noexcept(*this);
        }

        std::span<const DeviceStoreEntry> entries() const
        {
            require_usable();
            return entries_;
        }

        amr::AmrPlanScope scope() const
        {
            require_usable();
            return scope_;
        }

    private:
        friend class DeviceBlockStoreLifecycle;

        Candidate(DeviceBlockStoreLifecycle& owner, amr::AmrPlanScope scope,
                  std::vector<DeviceStoreEntry> entries,
                  std::vector<DeviceStoreEntry> retirement)
            : owner_(&owner), scope_(scope), entries_(std::move(entries))
        {
            prepared_retirement_.push_back(
                RetirementBatch{{}, std::move(retirement)});
        }

        void require_usable() const
        {
            if (owner_ == nullptr)
                throw std::logic_error(
                    "device store candidate is no longer usable");
        }

        DeviceBlockStoreLifecycle* owner_ = nullptr;
        amr::AmrPlanScope scope_{};
        std::vector<DeviceStoreEntry> entries_;
        std::list<RetirementBatch> prepared_retirement_;
        bool commit_attempted_ = false;
    };

    explicit DeviceBlockStoreLifecycle(
        std::span<const DeviceStoreEntry> initial)
        : active_entries_(initial.begin(), initial.end())
    {
        validate_initial_entries();
    }

    DeviceBlockStoreLifecycle(const DeviceBlockStoreLifecycle&) = delete;
    DeviceBlockStoreLifecycle& operator=(const DeviceBlockStoreLifecycle&)
        = delete;
    DeviceBlockStoreLifecycle(DeviceBlockStoreLifecycle&&) = delete;
    DeviceBlockStoreLifecycle& operator=(DeviceBlockStoreLifecycle&&) = delete;

    amr::TopologyEpoch active_epoch() const noexcept { return active_epoch_; }

    std::span<const DeviceStoreEntry> active_entries() const noexcept
    {
        return active_entries_;
    }

    bool has_staged_transaction() const noexcept
    {
        return staged_transaction_id_ != 0;
    }

    bool contains(backend::BackendStateAccess access) const noexcept
    {
        return find_entry(active_entries_, access) != nullptr;
    }

    const DeviceStoreEntry& record(
        backend::BackendStateAccess access) const
    {
        const auto* found = find_entry(active_entries_, access);
        if (found == nullptr)
            throw std::invalid_argument("stale active device store access");
        return *found;
    }

    Candidate prepare(amr::AmrPlanScope scope,
                      std::span<const DeviceStoreProposal> proposed)
    {
        require_not_mutating();
        if (has_staged_transaction())
            throw std::logic_error(
                "a device store transaction is already staged");
        if (scope.transaction_id == 0
            || !amr::is_valid(scope.from_epoch)
            || !amr::is_valid(scope.to_epoch)
            || scope.from_epoch != active_epoch_
            || scope.to_epoch.value <= scope.from_epoch.value)
            throw std::invalid_argument("invalid device store AMR scope");
        if (scope.transaction_id <= max_transaction_id_)
            throw std::invalid_argument(
                "replayed or non-monotonic device store transaction");
        if (proposed.empty())
            throw std::invalid_argument("staged device store is empty");

        std::set<amr::BlockHandle> unique_handles;
        std::set<backend::StorageGeneration> unique_storage;
        std::uint64_t proposed_storage_max = max_storage_generation_;
        for (const auto proposal : proposed) {
            if (!amr::is_valid(proposal.handle)
                || proposal.handle.epoch != scope.to_epoch
                || !backend::is_valid(proposal.storage)
                || proposal.storage.value <= max_storage_generation_
                || !unique_handles.insert(proposal.handle).second
                || !unique_storage.insert(proposal.storage).second)
                throw std::invalid_argument(
                    "invalid or duplicate staged device proposal");
            proposed_storage_max =
                std::max(proposed_storage_max, proposal.storage.value);
        }

        if (proposed.size()
            > std::numeric_limits<std::uint64_t>::max()
                - max_layout_generation_)
            throw std::overflow_error("device layout generation exhausted");

        const auto arenas = allocate_arena_slots(proposed.size());
        std::vector<DeviceStoreEntry> staged;
        staged.reserve(proposed.size());
        std::uint64_t proposed_layout_max = max_layout_generation_;
        for (std::size_t index = 0; index < proposed.size(); ++index) {
            staged.push_back(DeviceStoreEntry{
                DeviceBlockRecord{
                    proposed[index].handle, proposed[index].storage,
                    DeviceLayoutGeneration{++proposed_layout_max}},
                arenas[index]});
        }

        // Construct every potentially throwing owner before mutating the
        // lifecycle counters or exposing a staged transaction.
        Candidate candidate(
            *this, scope, std::move(staged), active_entries_);
        max_storage_generation_ = proposed_storage_max;
        max_layout_generation_ = proposed_layout_max;
        max_transaction_id_ = scope.transaction_id;
        staged_transaction_id_ = scope.transaction_id;
        return candidate;
    }

    const DeviceStoreEntry& migration_record(
        const Candidate& candidate, DeviceMigrationAccess migration) const
    {
        validate_candidate(candidate);
        if (migration.scope != candidate.scope_)
            throw std::invalid_argument("device migration scope mismatch");

        const DeviceStoreEntry* found = nullptr;
        switch (migration.role) {
        case DeviceMigrationRole::ActiveOldSource:
            if (migration.access.block.epoch != migration.scope.from_epoch)
                throw std::invalid_argument(
                    "old migration source epoch mismatch");
            found = find_entry(active_entries_, migration.access);
            break;
        case DeviceMigrationRole::StagedNewDestination:
            if (migration.access.block.epoch != migration.scope.to_epoch)
                throw std::invalid_argument(
                    "new migration destination epoch mismatch");
            found = find_entry(candidate.entries_, migration.access);
            break;
        }
        if (found == nullptr)
            throw std::invalid_argument("stale device migration access");
        return *found;
    }

    bool contains_migration(const Candidate& candidate,
                            DeviceMigrationAccess migration) const noexcept
    {
        try {
            (void)migration_record(candidate, migration);
            return true;
        } catch (...) {
            return false;
        }
    }

    void abort(Candidate&& candidate)
    {
        require_not_mutating();
        validate_candidate(candidate);
        staged_transaction_id_ = 0;
        poison(candidate);
    }

    template <typename Finalizer>
    void publish_after_success(Candidate&& candidate,
                               DeviceRetirementFence fence,
                               Finalizer&& finalizer)
    {
        require_not_mutating();
        validate_candidate(candidate);
        if (candidate.commit_attempted_)
            throw std::logic_error(
                "device store publication has already been attempted");
        if (max_fence_id_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("device retirement fence exhausted");
        if (fence.value == 0 || fence.value <= max_fence_id_)
            throw std::invalid_argument(
                "invalid or non-monotonic retirement fence");
        if (candidate.prepared_retirement_.size() != 1)
            throw std::logic_error("missing prepared device retirement batch");

        candidate.commit_attempted_ = true;
        candidate.prepared_retirement_.front().fence = fence;
        max_fence_id_ = fence.value;

        MutationGuard guard(*this);
        std::forward<Finalizer>(finalizer)(
            std::span<const DeviceStoreEntry>(candidate.entries_));

        // From here onward publication is allocation-free and non-throwing.
        active_entries_.swap(candidate.entries_);
        active_epoch_ = candidate.scope_.to_epoch;
        retirements_.splice(retirements_.end(),
                            candidate.prepared_retirement_);
        staged_transaction_id_ = 0;
        poison(candidate);
    }

    bool has_retirement(DeviceRetirementFence fence) const noexcept
    {
        return find_retirement(fence) != retirements_.end();
    }

    template <typename Finalizer>
    void complete_retirement(DeviceRetirementFence fence,
                             Finalizer&& finalizer)
    {
        static_assert(std::is_nothrow_invocable_v<
                      Finalizer, std::span<const DeviceStoreEntry>>,
                      "device retirement finalizer must be noexcept");
        require_not_mutating();
        auto found = find_retirement(fence);
        if (found == retirements_.end())
            throw std::invalid_argument("unknown device retirement fence");

        MutationGuard guard(*this);
        std::forward<Finalizer>(finalizer)(
            std::span<const DeviceStoreEntry>(found->entries));
        retirements_.erase(found);
    }

private:
    class MutationGuard {
    public:
        explicit MutationGuard(DeviceBlockStoreLifecycle& owner)
            : owner_(owner)
        {
            if (owner_.mutation_in_progress_)
                throw std::logic_error("reentrant device store mutation");
            owner_.mutation_in_progress_ = true;
        }

        ~MutationGuard() { owner_.mutation_in_progress_ = false; }

        MutationGuard(const MutationGuard&) = delete;
        MutationGuard& operator=(const MutationGuard&) = delete;

    private:
        DeviceBlockStoreLifecycle& owner_;
    };

    void validate_initial_entries()
    {
        if (active_entries_.empty())
            throw std::invalid_argument("device block store is empty");
        active_epoch_ = active_entries_.front().record.handle.epoch;
        if (!amr::is_valid(active_epoch_))
            throw std::invalid_argument("invalid active topology epoch");

        std::set<amr::BlockHandle> handles;
        std::set<backend::StorageGeneration> storage;
        std::set<DeviceLayoutGeneration> layouts;
        std::set<DeviceArenaSlot> arenas;
        for (const auto& entry : active_entries_) {
            if (!amr::is_valid(entry.record.handle)
                || entry.record.handle.epoch != active_epoch_
                || !backend::is_valid(entry.record.storage)
                || !is_valid(entry.record.layout) || entry.arena.value == 0
                || !handles.insert(entry.record.handle).second
                || !storage.insert(entry.record.storage).second
                || !layouts.insert(entry.record.layout).second
                || !arenas.insert(entry.arena).second)
                throw std::invalid_argument("invalid device store entry");
            max_storage_generation_ =
                std::max(max_storage_generation_, entry.record.storage.value);
            max_layout_generation_ =
                std::max(max_layout_generation_, entry.record.layout.value);
        }
    }

    void require_not_mutating() const
    {
        if (mutation_in_progress_)
            throw std::logic_error("reentrant device store mutation");
    }

    void validate_candidate(const Candidate& candidate) const
    {
        candidate.require_usable();
        if (candidate.owner_ != this
            || staged_transaction_id_ != candidate.scope_.transaction_id)
            throw std::invalid_argument("foreign or stale device candidate");
    }

    static void poison(Candidate& candidate) noexcept
    {
        candidate.owner_ = nullptr;
        candidate.scope_ = {};
        candidate.entries_.clear();
        candidate.prepared_retirement_.clear();
        candidate.commit_attempted_ = true;
    }

    void abandon_candidate_noexcept(Candidate& candidate) noexcept
    {
        if (candidate.owner_ == this
            && staged_transaction_id_ == candidate.scope_.transaction_id
            && !mutation_in_progress_)
            staged_transaction_id_ = 0;
        poison(candidate);
    }

    static const DeviceStoreEntry* find_entry(
        std::span<const DeviceStoreEntry> entries,
        backend::BackendStateAccess access) noexcept
    {
        for (const auto& entry : entries) {
            if (entry.record.handle == access.block
                && entry.record.storage == access.storage)
                return &entry;
        }
        return nullptr;
    }

    std::vector<DeviceArenaSlot> allocate_arena_slots(
        std::size_t count) const
    {
        std::set<DeviceArenaSlot> occupied;
        for (const auto& entry : active_entries_) occupied.insert(entry.arena);
        for (const auto& retirement : retirements_)
            for (const auto& entry : retirement.entries)
                occupied.insert(entry.arena);

        std::vector<DeviceArenaSlot> result;
        result.reserve(count);
        std::uint64_t candidate = 1;
        while (result.size() != count) {
            if (candidate == 0)
                throw std::overflow_error("device arena slot exhausted");
            const DeviceArenaSlot slot{candidate};
            if (!occupied.contains(slot)) {
                occupied.insert(slot);
                result.push_back(slot);
            }
            if (candidate == std::numeric_limits<std::uint64_t>::max()
                && result.size() != count)
                throw std::overflow_error("device arena slot exhausted");
            ++candidate;
        }
        return result;
    }

    std::list<RetirementBatch>::iterator find_retirement(
        DeviceRetirementFence fence) noexcept
    {
        return std::find_if(retirements_.begin(), retirements_.end(),
                            [fence](const RetirementBatch& batch) {
                                return batch.fence == fence;
                            });
    }

    std::list<RetirementBatch>::const_iterator find_retirement(
        DeviceRetirementFence fence) const noexcept
    {
        return std::find_if(retirements_.begin(), retirements_.end(),
                            [fence](const RetirementBatch& batch) {
                                return batch.fence == fence;
                            });
    }

    std::vector<DeviceStoreEntry> active_entries_;
    std::list<RetirementBatch> retirements_;
    amr::TopologyEpoch active_epoch_{};
    std::uint64_t staged_transaction_id_ = 0;
    std::uint64_t max_storage_generation_ = 0;
    std::uint64_t max_layout_generation_ = 0;
    std::uint64_t max_transaction_id_ = 0;
    std::uint64_t max_fence_id_ = 0;
    bool mutation_in_progress_ = false;
};

} // namespace arch::cuda
