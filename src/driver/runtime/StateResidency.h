/**
 * @file StateResidency.h
 * @brief Track logical state versions and host/device visibility.
 *
 * Interior and ghost regions are tracked independently for each block and
 * slot. Transfer completion and stage writes update this metadata explicitly;
 * the tracker performs neither data copies nor numerical evolution.
 * Workflow:
 * 1. Receive a resolved plan and active AMR topology.
 * 2. Manage backend residency and stable state generations.
 * 3. Expose stage data only through checked runtime leases.
 * 4. Snapshot exact metadata for one quiescent execution side while real
 *    field owners independently protect their Host or Device allocations.
 */

#pragma once

#include "amr/topology/BlockHandle.h"

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <utility>
#include <map>
#include <unordered_map>
#include <stdexcept>

namespace arch::state {

enum class StateSlot : std::uint8_t { Current, Next, Scratch };
enum class StateRegion : std::uint8_t { Interior, Ghost };
enum class ExecutionSide : std::uint8_t { Host, Device };

enum class StateResidency : std::uint8_t {
    HostValid,
    DeviceValid,
    Synchronized,
    Invalid
};

enum class PendingTransferPhase : std::uint8_t {
    None,
    PendingH2D,
    PendingD2H
};

enum class CompletionState : std::uint8_t { None, Pending, Complete };

struct StateVersion {
    std::uint64_t value = 0;
    friend constexpr auto operator<=>(const StateVersion&, const StateVersion&)
        = default;
};

struct CompletionToken {
    std::uint64_t value = 0;
    CompletionState state = CompletionState::None;
    friend constexpr auto operator<=>(const CompletionToken&,
                                      const CompletionToken&) = default;
};

struct StateKey {
    amr::BlockHandle block{};
    StateSlot slot = StateSlot::Current;
    friend constexpr auto operator<=>(const StateKey&, const StateKey&) = default;
};

struct RegionCoherence {
    StateResidency residency = StateResidency::Invalid;
    StateVersion version{};
    CompletionToken completion{};
    PendingTransferPhase pending_transfer = PendingTransferPhase::None;
};

struct SlotCoherence {
    RegionCoherence interior{};
    RegionCoherence ghost{};
    StateVersion ghost_source_version{};
};

struct StateReadRequirement {
    ExecutionSide side;
    StateVersion version;
    bool require_interior;
    bool require_ghost;
};

struct SlotRotation {
    StateSlot current_from;
    StateSlot next_from;
    StateSlot scratch_from;
};

constexpr bool is_valid(StateVersion version) noexcept
{
    return version.value != 0;
}

constexpr bool is_valid(CompletionToken token) noexcept
{
    return token.value != 0 && token.state != CompletionState::None;
}

constexpr bool is_pending(CompletionToken token) noexcept
{
    return is_valid(token) && token.state == CompletionState::Pending;
}

constexpr bool is_complete(CompletionToken token) noexcept
{
    return is_valid(token) && token.state == CompletionState::Complete;
}

constexpr bool side_can_read(StateResidency residency,
                             ExecutionSide side) noexcept
{
    if (side != ExecutionSide::Host && side != ExecutionSide::Device)
        return false;
    if (residency == StateResidency::Synchronized) return true;
    if (side == ExecutionSide::Host)
        return residency == StateResidency::HostValid;
    if (side == ExecutionSide::Device)
        return residency == StateResidency::DeviceValid;
    return false;
}

constexpr RegionCoherence invalid_region() noexcept
{
    return {};
}

constexpr SlotCoherence invalid_slot() noexcept
{
    return {};
}

class StateResidencyLedger {
public:
    explicit StateResidencyLedger(amr::TopologyEpoch active_epoch)
        : active_epoch_(active_epoch)
    {
        if (!amr::is_valid(active_epoch_))
            throw std::invalid_argument("state residency epoch must be nonzero");
    }

    StateResidencyLedger(const StateResidencyLedger&) = delete;
    StateResidencyLedger& operator=(const StateResidencyLedger&) = delete;
    StateResidencyLedger(StateResidencyLedger&&) = delete;
    StateResidencyLedger& operator=(StateResidencyLedger&&) = delete;

    amr::TopologyEpoch active_epoch() const noexcept
    {
        return active_epoch_;
    }

    void register_block(amr::BlockHandle block,
                        StateVersion current_version,
                        CompletionToken completed_initialization,
                        ExecutionSide initial_side = ExecutionSide::Host)
    {
        if(snapshot_owner_)throw std::logic_error("Metadata snapshot excludes topology registration");
        validate_side(initial_side);
        validate_handle_epoch(block);
        if (!is_valid(current_version))
            throw std::invalid_argument("initial state version must be nonzero");
        if (!is_complete(completed_initialization))
            throw std::invalid_argument(
                "initial state publication requires a completed token");
        if (blocks_.contains(block.uid.value))
            throw std::invalid_argument(
                "block is already registered or permanently retired");

        BlockRecord record{};
        record.active = true;
        Entry& current = record.slots[slot_index(StateSlot::Current)];
        current.coherence.interior = {
            residency_for(initial_side), current_version,
            completed_initialization, PendingTransferPhase::None};
        current.interior_last_token_id = completed_initialization.value;
        blocks_.emplace(block.uid.value, std::move(record));
    }

    void retire_block(amr::BlockHandle block)
    {
        if(snapshot_owner_)throw std::logic_error("Metadata snapshot excludes topology retirement");
        BlockRecord& record = require_record(block);
        quiesce(block);
        record.active = false; // Keep the UID as a same-epoch tombstone.
    }

    SlotCoherence inspect(StateKey key) const
    {
        return require_entry(key).coherence;
    }

    void require_readable(StateKey key,
                          const StateReadRequirement& requirement) const
    {
        validate_side(requirement.side);
        if (!is_valid(requirement.version))
            throw std::logic_error("read version must be nonzero");
        if (!requirement.require_interior && !requirement.require_ghost)
            throw std::logic_error("read must require interior or ghost");

        const SlotCoherence& slot = require_entry(key).coherence;
        if (requirement.require_interior || requirement.require_ghost)
            require_region_readable(slot.interior, requirement.side,
                                    requirement.version);
        if (requirement.require_ghost) {
            require_region_readable(slot.ghost, requirement.side,
                                    requirement.version);
            if (slot.ghost.version != slot.interior.version
                || slot.ghost_source_version != slot.interior.version
                || slot.ghost_source_version != requirement.version) {
                throw std::logic_error(
                    "ghost does not correspond to the requested interior");
            }
        }
    }

    void publish_interior(StateKey key, ExecutionSide side,
                          StateVersion new_version,
                          CompletionToken completed_operation)
    {
        if(snapshot_owner_&&side!=snapshot_owner_->side_)
            throw std::logic_error("Metadata snapshot excludes publication from the other side");
        validate_side(side);
        if (!is_valid(new_version))
            throw std::logic_error("published version must be nonzero");
        if (!is_complete(completed_operation))
            throw std::logic_error("interior publication requires completion");

        Entry& entry = require_entry(key);
        require_slot_without_pending(entry.coherence);
        if (entry.coherence.interior.residency != StateResidency::Invalid
            && new_version <= entry.coherence.interior.version) {
            throw std::logic_error("interior version must increase");
        }
        require_new_token(completed_operation.value,
                          entry.interior_last_token_id);

        entry.interior_last_token_id = completed_operation.value;
        entry.coherence.interior = {
            residency_for(side), new_version, completed_operation,
            PendingTransferPhase::None};
        entry.coherence.ghost = invalid_region();
        entry.coherence.ghost_source_version = {};
    }

    void publish_ghost(StateKey key, ExecutionSide side,
                       StateVersion source_version,
                       CompletionToken completed_operation)
    {
        if(snapshot_owner_&&side!=snapshot_owner_->side_)
            throw std::logic_error("Metadata snapshot excludes publication from the other side");
        validate_side(side);
        if (!is_valid(source_version))
            throw std::logic_error("ghost source version must be nonzero");
        if (!is_complete(completed_operation))
            throw std::logic_error("ghost publication requires completion");

        Entry& entry = require_entry(key);
        require_slot_without_pending(entry.coherence);
        require_region_readable(entry.coherence.interior, side, source_version);
        if (source_version != entry.coherence.interior.version)
            throw std::logic_error("ghost source version must match interior");
        require_new_token(completed_operation.value,
                          entry.ghost_last_token_id);

        entry.ghost_last_token_id = completed_operation.value;
        entry.coherence.ghost = {
            residency_for(side), source_version, completed_operation,
            PendingTransferPhase::None};
        entry.coherence.ghost_source_version = source_version;
    }

    void begin_transfer(StateKey key, StateRegion region,
                        PendingTransferPhase direction,
                        CompletionToken pending_operation)
    {
        if(snapshot_owner_)throw std::logic_error("Metadata snapshot excludes asynchronous transfer");
        validate_region(region);
        if (direction != PendingTransferPhase::PendingH2D
            && direction != PendingTransferPhase::PendingD2H) {
            throw std::logic_error("transfer direction must be H2D or D2H");
        }
        if (!is_pending(pending_operation))
            throw std::logic_error("transfer begin requires a pending token");

        Entry& entry = require_entry(key);
        RegionCoherence& coherence = select_region(entry.coherence, region);
        std::uint64_t& high_watermark = select_high_watermark(entry, region);
        if (coherence.pending_transfer != PendingTransferPhase::None)
            throw std::logic_error("region already has a pending transfer");
        if (direction == PendingTransferPhase::PendingH2D
            && coherence.residency != StateResidency::HostValid) {
            throw std::logic_error("H2D requires HostValid source");
        }
        if (direction == PendingTransferPhase::PendingD2H
            && coherence.residency != StateResidency::DeviceValid) {
            throw std::logic_error("D2H requires DeviceValid source");
        }
        require_new_token(pending_operation.value, high_watermark);

        high_watermark = pending_operation.value;
        coherence.completion = pending_operation;
        coherence.pending_transfer = direction;
    }

    void complete_transfer(StateKey key, StateRegion region,
                           CompletionToken completed_operation)
    {
        validate_region(region);
        if (!is_complete(completed_operation))
            throw std::logic_error("transfer completion requires complete token");

        Entry& entry = require_entry(key);
        RegionCoherence& coherence = select_region(entry.coherence, region);
        if (coherence.pending_transfer == PendingTransferPhase::None
            || !is_pending(coherence.completion)
            || completed_operation.value != coherence.completion.value) {
            throw std::logic_error("completion does not match pending transfer");
        }

        coherence.residency = StateResidency::Synchronized;
        coherence.completion = completed_operation;
        coherence.pending_transfer = PendingTransferPhase::None;
    }

    void quiesce() const
    {
        for (const auto& [uid, record] : blocks_) {
            if (!record.active) continue;
            for (const Entry& entry : record.slots)
                validate_quiescent(entry.coherence);
        }
    }

    void quiesce(amr::BlockHandle block) const
    {
        const BlockRecord& record = require_record(block);
        for (const Entry& entry : record.slots)
            validate_quiescent(entry.coherence);
    }

    void materialize_host_current(amr::BlockHandle block,
                                  StateRegion region,
                                  CompletionToken pending_operation)
    {
        begin_transfer({block, StateSlot::Current}, region,
                       PendingTransferPhase::PendingD2H, pending_operation);
    }

    void complete_materialize_host_current(
        amr::BlockHandle block, StateRegion region,
        CompletionToken completed_operation)
    {
        require_direction({block, StateSlot::Current}, region,
                          PendingTransferPhase::PendingD2H);
        complete_transfer({block, StateSlot::Current}, region,
                          completed_operation);
    }

    void upload_slot(StateKey key, StateRegion region,
                     CompletionToken pending_operation)
    {
        begin_transfer(key, region, PendingTransferPhase::PendingH2D,
                       pending_operation);
    }

    void complete_upload_slot(StateKey key, StateRegion region,
                              CompletionToken completed_operation)
    {
        require_direction(key, region, PendingTransferPhase::PendingH2D);
        complete_transfer(key, region, completed_operation);
    }

    void rotate_slots(amr::BlockHandle block, SlotRotation rotation)
    {
        BlockRecord& record = require_record(block);
        validate_rotation(rotation);
        for (const Entry& entry : record.slots)
            validate_quiescent(entry.coherence);

        const auto old = record.slots;
        const std::array<StateSlot, 3> sources{
            rotation.current_from, rotation.next_from, rotation.scratch_from};
        for (std::size_t destination = 0; destination < old.size();
             ++destination) {
            const std::size_t source = slot_index(sources[destination]);
            Entry& output = record.slots[destination];
            output.coherence = old[source].coherence;
            output.interior_last_token_id = merged_high_watermark(
                old[destination].interior_last_token_id,
                old[source].interior_last_token_id,
                old[source].coherence.interior);
            output.ghost_last_token_id = merged_high_watermark(
                old[destination].ghost_last_token_id,
                old[source].ghost_last_token_id,
                old[source].coherence.ghost);
        }
    }

private:
    struct Entry {
        SlotCoherence coherence = invalid_slot();
        std::uint64_t interior_last_token_id = 0;
        std::uint64_t ghost_last_token_id = 0;
    };

    // One epoch owns each UID and all three slots. Retired records remain
    // tombstones, while hot stage lookups need only one hash probe per block.
    struct BlockRecord {
        std::array<Entry, 3> slots{};
        bool active = false;
    };
public:
    /**
     * Metadata-only savepoint bound to this ledger and one execution side.
     * Records include retired UIDs, all slots and private token high-watermarks;
     * field allocations and backend completion are protected by their real owners.
     * Workflow:
     * 1. Prepare a quiescent snapshot for the actual mutation side.
     * 2. Prepare all field/Runtime backups before freezing this ledger snapshot.
     * 3. Publish only on the frozen side, then release or restore the same backup.
     * 4. Use strict owner matching for leases/witnesses; compare records alone only
     *    for an explicit independent Runtime reference, never to authorize restore.
     */
    class MetadataSnapshot {
        friend class StateResidencyLedger;
        const StateResidencyLedger* const owner_;
        const ExecutionSide side_;
        amr::TopologyEpoch epoch_{};
        std::unordered_map<std::uint64_t, BlockRecord> records_;
        /** Copy complete ledger history without claiming protection of field storage. */
        explicit MetadataSnapshot(const StateResidencyLedger& owner, ExecutionSide side)
            : owner_(&owner),side_(side),epoch_(owner.active_epoch_),records_(owner.blocks_) {}
    public:
        MetadataSnapshot(const MetadataSnapshot&)=delete;
        MetadataSnapshot& operator=(const MetadataSnapshot&)=delete;
        MetadataSnapshot(MetadataSnapshot&&)=delete;
        MetadataSnapshot& operator=(MetadataSnapshot&&)=delete;
    };
    /**
     * Prepare a backup only when all active metadata is settled on the requested side.
     * Host remains strictly HostValid/Invalid. Device admits DeviceValid/Synchronized/
     * Invalid, but Synchronized restoration additionally requires protected Device
     * fields and an unchanged or independently protected Host mirror at Runtime.
     * This method does not synchronize streams or grant any field-storage lease.
     */
    MetadataSnapshot snapshot_metadata(ExecutionSide side) const {
        validate_side(side);
        if(snapshot_owner_)throw std::logic_error("Metadata residency backup already leased");
        for(const auto& [uid,record]:blocks_) {
            if(!record.active)continue;
            for(const auto& slot:record.slots) {
                validate_quiescent(slot.coherence);
                for(const auto& region:{slot.coherence.interior,slot.coherence.ghost}) {
                    const bool allowed=region.residency==StateResidency::Invalid
                        ||(side==ExecutionSide::Host
                            ?region.residency==StateResidency::HostValid
                            :region.residency==StateResidency::DeviceValid
                                ||region.residency==StateResidency::Synchronized);
                    if(!allowed)
                        throw std::logic_error(side==ExecutionSide::Host
                            ?"Host metadata backup excludes Device/Synchronized residency"
                            :"Device metadata backup excludes Host residency");
                }
            }
        }
        return MetadataSnapshot(*this,side);
    }
    /**
     * Freeze only this owner's exact, still-current backup after field allocation succeeds.
     * The lease excludes topology/asynchronous transfer mutation; interior and ghost
     * publications retain the original actual-side residency downgrade on same-side writes.
     */
    void freeze_metadata_snapshot(const MetadataSnapshot& snapshot) {
        if(snapshot_owner_||!metadata_snapshot_matches(snapshot))
            throw std::logic_error("Metadata residency backup is foreign, stale or overlapping");
        snapshot_owner_=&snapshot;
    }
    /**
     * Restore all prepared records and the epoch by the original noexcept ownership swap.
     * Runtime must first drain work and restore the actual fields/mirrors protected by
     * its transaction; only the exact leased snapshot may restore this metadata.
     */
    void restore_metadata_snapshot_noexcept(MetadataSnapshot& snapshot) noexcept {
        if(snapshot_owner_!=&snapshot)std::terminate();
        blocks_.swap(snapshot.records_);active_epoch_=snapshot.epoch_;
    }
    /** Release only the exact leased backup while its lifetime still covers this ledger. */
    void release_metadata_snapshot(const MetadataSnapshot& snapshot) noexcept {
        if(snapshot_owner_!=&snapshot)std::terminate();
        snapshot_owner_=nullptr;
    }
    /**
     * Match exact owner, valid frozen side, epoch and complete record/token history.
     * Do not reapply side admission to live records: permitted same-side publications
     * legitimately change metadata and must simply make this equality check false.
     */
    bool metadata_snapshot_matches(const MetadataSnapshot& snapshot) const noexcept {
        return snapshot.owner_==this
            &&(snapshot.side_==ExecutionSide::Host||snapshot.side_==ExecutionSide::Device)
            &&metadata_records_match(snapshot);
    }
    /**
     * Compare exact epoch/records/token history for independent reference checks.
     * Content equality intentionally does not require the same ledger owner or side.
     * It grants no snapshot lease or restore identity; freeze/restore consumers must
     * use metadata_snapshot_matches and the exact leased snapshot respectively.
     */
    bool metadata_records_match(const MetadataSnapshot& snapshot) const noexcept {
        const auto same_region=[](const RegionCoherence& a,const RegionCoherence& b) {
            return a.residency==b.residency&&a.version==b.version
                &&a.completion==b.completion&&a.pending_transfer==b.pending_transfer;
        };
        if(active_epoch_!=snapshot.epoch_||blocks_.size()!=snapshot.records_.size())return false;
        for(const auto& [uid,record]:blocks_) {
            const auto found=snapshot.records_.find(uid);
            if(found==snapshot.records_.end()||record.active!=found->second.active)return false;
            for(std::size_t s=0;s<3;++s) {
                const auto& a=record.slots[s];const auto& b=found->second.slots[s];
                if(a.interior_last_token_id!=b.interior_last_token_id
                    ||a.ghost_last_token_id!=b.ghost_last_token_id
                    ||a.coherence.ghost_source_version!=b.coherence.ghost_source_version
                    ||!same_region(a.coherence.interior,b.coherence.interior)
                    ||!same_region(a.coherence.ghost,b.coherence.ghost))return false;
            }
        }
        return true;
    }

private:

    static constexpr std::array<StateSlot, 3> all_slots() noexcept
    {
        return {StateSlot::Current, StateSlot::Next, StateSlot::Scratch};
    }

    static constexpr std::size_t slot_index(StateSlot slot)
    {
        switch (slot) {
        case StateSlot::Current: return 0;
        case StateSlot::Next: return 1;
        case StateSlot::Scratch: return 2;
        }
        throw std::logic_error("invalid state slot");
    }

    static void validate_rotation(SlotRotation rotation)
    {
        const std::array<StateSlot, 3> sources{
            rotation.current_from, rotation.next_from, rotation.scratch_from};
        for (StateSlot source : sources) (void) slot_index(source);
        if (sources[0] == sources[1] || sources[0] == sources[2]
            || sources[1] == sources[2]) {
            throw std::logic_error("slot rotation must be a permutation");
        }
    }

    static void validate_side(ExecutionSide side)
    {
        if (side != ExecutionSide::Host && side != ExecutionSide::Device)
            throw std::logic_error("invalid execution side");
    }

    static void validate_region(StateRegion region)
    {
        if (region != StateRegion::Interior && region != StateRegion::Ghost)
            throw std::logic_error("invalid state region");
    }

    static StateResidency residency_for(ExecutionSide side) noexcept
    {
        return side == ExecutionSide::Host ? StateResidency::HostValid
                                           : StateResidency::DeviceValid;
    }

    static void require_new_token(std::uint64_t token,
                                  std::uint64_t high_watermark)
    {
        if (token == 0 || token <= high_watermark)
            throw std::logic_error("operation token must exceed region history");
    }

    static void require_region_readable(const RegionCoherence& region,
                                        ExecutionSide side,
                                        StateVersion version)
    {
        if (!side_can_read(region.residency, side)
            || region.version != version
            || region.pending_transfer != PendingTransferPhase::None
            || !is_complete(region.completion)) {
            throw std::logic_error("state region is not readable");
        }
    }

    static void require_slot_without_pending(const SlotCoherence& slot)
    {
        if (slot.interior.pending_transfer != PendingTransferPhase::None
            || slot.ghost.pending_transfer != PendingTransferPhase::None
            || is_pending(slot.interior.completion)
            || is_pending(slot.ghost.completion)) {
            throw std::logic_error("slot has a pending transfer");
        }
    }

    static void validate_region_encoding(const RegionCoherence& region)
    {
        if (region.residency == StateResidency::Invalid) {
            if (is_valid(region.version) || is_valid(region.completion)
                || region.pending_transfer != PendingTransferPhase::None) {
                throw std::logic_error("invalid region has stale public state");
            }
            return;
        }
        if (!is_valid(region.version) || !is_valid(region.completion))
            throw std::logic_error("valid region lacks version or completion");
        if (region.pending_transfer == PendingTransferPhase::None
            && !is_complete(region.completion)) {
            throw std::logic_error("settled region lacks completed token");
        }
        if (region.pending_transfer != PendingTransferPhase::None
            && !is_pending(region.completion)) {
            throw std::logic_error("pending region lacks pending token");
        }
    }

    static void validate_quiescent(const SlotCoherence& slot)
    {
        validate_region_encoding(slot.interior);
        validate_region_encoding(slot.ghost);
        require_slot_without_pending(slot);
        if (slot.ghost.residency == StateResidency::Invalid) {
            if (is_valid(slot.ghost_source_version))
                throw std::logic_error("invalid ghost retains source version");
        } else if (slot.ghost.version != slot.ghost_source_version
                   || !is_valid(slot.ghost_source_version)) {
            throw std::logic_error("ghost source version is inconsistent");
        }
    }

    static RegionCoherence& select_region(SlotCoherence& slot,
                                          StateRegion region)
    {
        return region == StateRegion::Interior ? slot.interior : slot.ghost;
    }

    static const RegionCoherence& select_region(const SlotCoherence& slot,
                                                StateRegion region)
    {
        return region == StateRegion::Interior ? slot.interior : slot.ghost;
    }

    static std::uint64_t& select_high_watermark(Entry& entry,
                                                StateRegion region)
    {
        return region == StateRegion::Interior
            ? entry.interior_last_token_id
            : entry.ghost_last_token_id;
    }

    static std::uint64_t merged_high_watermark(
        std::uint64_t old_destination, std::uint64_t source,
        const RegionCoherence& incoming) noexcept
    {
        const std::uint64_t public_completion =
            incoming.residency == StateResidency::Invalid
                ? 0
                : incoming.completion.value;
        return std::max({old_destination, source, public_completion});
    }

    void validate_handle_epoch(amr::BlockHandle block) const
    {
        if (!amr::is_valid(block) || block.epoch != active_epoch_)
            throw std::invalid_argument("invalid or stale block handle");
    }

    BlockRecord& require_record(amr::BlockHandle block)
    {
        validate_handle_epoch(block);
        const auto found = blocks_.find(block.uid.value);
        if (found == blocks_.end() || !found->second.active)
            throw std::invalid_argument("block is unknown or retired");
        return found->second;
    }

    const BlockRecord& require_record(amr::BlockHandle block) const
    {
        validate_handle_epoch(block);
        const auto found = blocks_.find(block.uid.value);
        if (found == blocks_.end() || !found->second.active)
            throw std::invalid_argument("block is unknown or retired");
        return found->second;
    }

    Entry& require_entry(StateKey key)
    {
        const std::size_t index = slot_index(key.slot);
        return require_record(key.block).slots[index];
    }

    const Entry& require_entry(StateKey key) const
    {
        const std::size_t index = slot_index(key.slot);
        return require_record(key.block).slots[index];
    }

    void require_direction(StateKey key, StateRegion region,
                           PendingTransferPhase expected) const
    {
        validate_region(region);
        const RegionCoherence& coherence =
            select_region(require_entry(key).coherence, region);
        if (coherence.pending_transfer != expected)
            throw std::logic_error("transfer completion direction mismatch");
    }

    amr::TopologyEpoch active_epoch_{};
    std::unordered_map<std::uint64_t, BlockRecord> blocks_;
    const MetadataSnapshot* snapshot_owner_=nullptr;
};

} // namespace arch::state
