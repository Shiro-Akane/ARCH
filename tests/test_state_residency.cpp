#include "driver/StateResidency.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace {

using arch::state::CompletionState;
using arch::state::CompletionToken;
using arch::state::ExecutionSide;
using arch::state::PendingTransferPhase;
using arch::state::RegionCoherence;
using arch::state::SlotCoherence;
using arch::state::SlotRotation;
using arch::state::StateKey;
using arch::state::StateReadRequirement;
using arch::state::StateRegion;
using arch::state::StateResidency;
using arch::state::StateResidencyLedger;
using arch::state::StateSlot;
using arch::state::StateVersion;

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <class Function>
void expect_throws(Function&& function, std::string_view message)
{
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    std::cerr << "FAIL: expected exception: " << message << '\n';
    ++failures;
}

template <class Function>
void run_case(std::string_view name, Function&& function)
{
    try {
        function();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: uncaught exception in " << name << ": "
                  << error.what() << '\n';
        ++failures;
    }
}

constexpr StateVersion version(std::uint64_t value) noexcept
{
    return StateVersion{value};
}

constexpr CompletionToken pending(std::uint64_t value) noexcept
{
    return CompletionToken{value, CompletionState::Pending};
}

constexpr CompletionToken complete(std::uint64_t value) noexcept
{
    return CompletionToken{value, CompletionState::Complete};
}

constexpr amr::BlockHandle handle(std::uint64_t uid, std::uint64_t epoch) noexcept
{
    return {{uid}, {epoch}};
}

constexpr StateKey key(amr::BlockHandle block, StateSlot slot) noexcept
{
    return {block, slot};
}

bool same_region(const RegionCoherence& left, const RegionCoherence& right)
{
    return left.residency == right.residency
        && left.version == right.version
        && left.completion == right.completion
        && left.pending_transfer == right.pending_transfer;
}

bool same_slot(const SlotCoherence& left, const SlotCoherence& right)
{
    return same_region(left.interior, right.interior)
        && same_region(left.ghost, right.ghost)
        && left.ghost_source_version == right.ghost_source_version;
}

std::array<SlotCoherence, 3> snapshot_all(
    const StateResidencyLedger& ledger, amr::BlockHandle block)
{
    return {
        ledger.inspect(key(block, StateSlot::Current)),
        ledger.inspect(key(block, StateSlot::Next)),
        ledger.inspect(key(block, StateSlot::Scratch))};
}

bool same_snapshot(const std::array<SlotCoherence, 3>& left,
                   const std::array<SlotCoherence, 3>& right)
{
    for (std::size_t index = 0; index < left.size(); ++index)
        if (!same_slot(left[index], right[index])) return false;
    return true;
}

struct TransferExecutionContext {
    std::size_t explicit_transfer_calls = 0;

    void reset() noexcept
    {
        explicit_transfer_calls = 0;
    }
};

class TestTransferExecutor {
public:
    TestTransferExecutor(StateResidencyLedger& ledger,
                         TransferExecutionContext& context) noexcept
        : ledger_(ledger), context_(context)
    {
    }

    void materialize_host_current(amr::BlockHandle block,
                                  StateRegion region,
                                  CompletionToken pending_operation)
    {
        ++context_.explicit_transfer_calls;
        ledger_.materialize_host_current(block, region, pending_operation);
    }

    void upload_slot(StateKey key, StateRegion region,
                     CompletionToken pending_operation)
    {
        ++context_.explicit_transfer_calls;
        ledger_.upload_slot(key, region, pending_operation);
    }

private:
    StateResidencyLedger& ledger_;
    TransferExecutionContext& context_;
};

void test_value_contract_and_helpers()
{
    static_assert(std::is_standard_layout_v<StateVersion>);
    static_assert(std::is_trivially_copyable_v<StateVersion>);
    static_assert(std::is_standard_layout_v<CompletionToken>);
    static_assert(std::is_trivially_copyable_v<CompletionToken>);
    static_assert(std::is_standard_layout_v<StateKey>);
    static_assert(std::is_trivially_copyable_v<StateKey>);
    static_assert(std::is_standard_layout_v<RegionCoherence>);
    static_assert(std::is_trivially_copyable_v<RegionCoherence>);
    static_assert(std::is_standard_layout_v<SlotCoherence>);
    static_assert(std::is_trivially_copyable_v<SlotCoherence>);
    static_assert(std::is_standard_layout_v<StateReadRequirement>);
    static_assert(std::is_trivially_copyable_v<StateReadRequirement>);
    static_assert(std::is_standard_layout_v<SlotRotation>);
    static_assert(std::is_trivially_copyable_v<SlotRotation>);
    static_assert(!std::is_copy_constructible_v<StateResidencyLedger>);
    static_assert(!std::is_copy_assignable_v<StateResidencyLedger>);
    static_assert(!std::is_move_constructible_v<StateResidencyLedger>);
    static_assert(!std::is_move_assignable_v<StateResidencyLedger>);

    expect(!arch::state::is_valid(version(0)), "zero version is invalid");
    expect(arch::state::is_valid(version(1)), "nonzero version is valid");
    expect(!arch::state::is_valid(CompletionToken{}), "empty token is invalid");
    expect(!arch::state::is_valid({7, CompletionState::None}),
           "nonzero token with None state is invalid");
    expect(!arch::state::is_valid({0, CompletionState::Complete}),
           "zero completed token is invalid");
    expect(arch::state::is_pending(pending(7)), "pending token is pending");
    expect(!arch::state::is_complete(pending(7)), "pending token is not complete");
    expect(arch::state::is_complete(complete(7)), "complete token is complete");

    expect(arch::state::side_can_read(StateResidency::HostValid,
                                      ExecutionSide::Host),
           "Host reads HostValid");
    expect(!arch::state::side_can_read(StateResidency::HostValid,
                                       ExecutionSide::Device),
           "Device cannot read HostValid");
    expect(arch::state::side_can_read(StateResidency::DeviceValid,
                                      ExecutionSide::Device),
           "Device reads DeviceValid");
    expect(!arch::state::side_can_read(StateResidency::DeviceValid,
                                       ExecutionSide::Host),
           "Host cannot read DeviceValid");
    expect(arch::state::side_can_read(StateResidency::Synchronized,
                                      ExecutionSide::Host)
               && arch::state::side_can_read(StateResidency::Synchronized,
                                              ExecutionSide::Device),
           "both sides read one synchronized logical authority");
    expect(!arch::state::side_can_read(StateResidency::Invalid,
                                       ExecutionSide::Host),
           "Invalid is unreadable");
    expect(!arch::state::side_can_read(
               StateResidency::Synchronized,
               static_cast<ExecutionSide>(255)),
           "an invalid execution side cannot read synchronized state");

    const RegionCoherence invalid = arch::state::invalid_region();
    expect(invalid.residency == StateResidency::Invalid
               && invalid.version.value == 0
               && invalid.completion.value == 0
               && invalid.completion.state == CompletionState::None
               && invalid.pending_transfer == PendingTransferPhase::None,
           "invalid region has canonical zero encoding");
    const SlotCoherence invalid_state = arch::state::invalid_slot();
    expect(same_region(invalid_state.interior, invalid)
               && same_region(invalid_state.ghost, invalid)
               && invalid_state.ghost_source_version.value == 0,
           "invalid slot has canonical region encoding");

    expect_throws([] { StateResidencyLedger invalid_epoch{{0}}; },
                  "zero topology epoch is rejected");

    StateResidencyLedger invalid_registration{{1}};
    expect_throws([&] {
        invalid_registration.register_block(handle(1, 1), version(0), complete(1));
    }, "zero registration version is rejected");
    expect_throws([&] {
        invalid_registration.register_block(handle(1, 1), version(1), pending(1));
    }, "pending registration token is rejected");
}

void test_registration_snapshots_and_reads()
{
    StateResidencyLedger ledger{{1}};
    const auto first = handle(10, 1);
    const auto second = handle(11, 1);
    ledger.register_block(first, version(1), complete(10));
    ledger.register_block(second, version(1), complete(10));

    const SlotCoherence current = ledger.inspect(key(first, StateSlot::Current));
    expect(current.interior.residency == StateResidency::HostValid
               && current.interior.version == version(1)
               && current.interior.completion == complete(10),
           "registration publishes Current Host interior");
    expect(current.ghost.residency == StateResidency::Invalid
               && current.ghost_source_version.value == 0,
           "registration leaves Current ghost invalid");
    expect(same_slot(ledger.inspect(key(first, StateSlot::Next)),
                     arch::state::invalid_slot()),
           "registration leaves Next invalid");
    expect(same_slot(ledger.inspect(key(first, StateSlot::Scratch)),
                     arch::state::invalid_slot()),
           "registration leaves Scratch invalid");

    expect_throws([&] { ledger.register_block(first, version(1), complete(11)); },
                  "duplicate registration is rejected");
    expect_throws([&] { ledger.register_block(handle(12, 2), version(1), complete(1)); },
                  "wrong epoch registration is rejected");

    SlotCoherence detached = ledger.inspect(key(first, StateSlot::Current));
    detached.interior.residency = StateResidency::DeviceValid;
    expect(ledger.inspect(key(first, StateSlot::Current)).interior.residency
               == StateResidency::HostValid,
           "inspect returns an independent value snapshot");

    const auto before = snapshot_all(ledger, first);
    ledger.require_readable(
        key(first, StateSlot::Current),
        {ExecutionSide::Host, version(1), true, false});
    (void) ledger.inspect(key(first, StateSlot::Next));
    const auto after = snapshot_all(ledger, first);
    expect(same_snapshot(before, after),
           "inspect and require_readable preserve the full ledger snapshot");

    expect_throws([&] {
        ledger.require_readable(
            key(first, StateSlot::Current),
            {ExecutionSide::Host, version(1), false, false});
    }, "empty read requirement is rejected");
    expect_throws([&] {
        ledger.require_readable(
            key(first, StateSlot::Current),
            {ExecutionSide::Host, version(1), true, true});
    }, "unrefreshed Current ghost is unreadable");
    expect_throws([&] {
        ledger.require_readable(
            key(first, StateSlot::Next),
            {ExecutionSide::Host, version(1), true, false});
    }, "Invalid Next is unreadable");
    expect_throws([&] { (void) ledger.inspect(key(handle(10, 2), StateSlot::Current)); },
                  "stale epoch lookup is rejected");
    expect_throws([&] { (void) ledger.inspect(key(handle(99, 1), StateSlot::Current)); },
                  "unknown block lookup is rejected");
}

void test_publication_and_region_versions()
{
    StateResidencyLedger ledger{{1}};
    const auto block = handle(20, 1);
    ledger.register_block(block, version(1), complete(10));
    const StateKey current = key(block, StateSlot::Current);
    const StateKey next = key(block, StateSlot::Next);
    const StateKey scratch = key(block, StateSlot::Scratch);
    const CompletionToken next_ghost_operation = complete(21);
    const CompletionToken next_stage_operation = complete(22);

    expect_throws([&] {
        ledger.publish_ghost(current, ExecutionSide::Host,
                             version(2), complete(11));
    }, "ghost source version must match interior");
    expect_throws([&] {
        ledger.publish_ghost(current, ExecutionSide::Host,
                             version(1), pending(11));
    }, "ghost publication rejects an incomplete token");
    ledger.publish_ghost(current, ExecutionSide::Host,
                         version(1), complete(11));
    ledger.require_readable(
        current, {ExecutionSide::Host, version(1), true, true});
    expect_throws([&] {
        ledger.require_readable(
            current, {ExecutionSide::Device, version(1), true, true});
    }, "Host publication leaves Device stale");

    ledger.publish_interior(next, ExecutionSide::Host,
                            version(2), complete(20));
    expect(ledger.inspect(next).ghost.residency == StateResidency::Invalid,
           "interior publication invalidates output ghost");
    expect_throws([&] {
        ledger.publish_ghost(next, ExecutionSide::Host,
                             version(1), complete(21));
    }, "old ghost version is rejected");
    ledger.publish_ghost(next, ExecutionSide::Host,
                         version(2), next_ghost_operation);
    ledger.require_readable(
        next, {ExecutionSide::Host, version(2), true, true});

    ledger.publish_interior(scratch, ExecutionSide::Device,
                            version(3), complete(30));
    ledger.publish_ghost(scratch, ExecutionSide::Device,
                         version(3), complete(31));
    ledger.require_readable(
        scratch, {ExecutionSide::Device, version(3), true, true});
    expect_throws([&] {
        ledger.require_readable(
            scratch, {ExecutionSide::Host, version(3), true, true});
    }, "Device publication leaves Host stale");

    expect(next_stage_operation.value > next_ghost_operation.value,
           "distinct scheduler operations use increasing token IDs");
    ledger.publish_interior(next, ExecutionSide::Host,
                            version(3), next_stage_operation);
    expect(ledger.inspect(next).ghost.residency == StateResidency::Invalid
               && ledger.inspect(next).ghost_source_version.value == 0,
           "a new stage operation invalidates the prior ghost publication");
    expect_throws([&] {
        ledger.publish_interior(next, ExecutionSide::Host,
                                version(4), next_stage_operation);
    }, "same-region token replay is rejected");
    ledger.publish_interior(next, ExecutionSide::Host,
                            version(4), complete(23));
    expect_throws([&] {
        ledger.publish_interior(next, ExecutionSide::Host,
                                version(0), complete(24));
    }, "zero publication version is rejected");
    expect_throws([&] {
        ledger.publish_interior(next, ExecutionSide::Host,
                                version(4), complete(25));
    }, "non-monotonic publication version is rejected");
    ledger.publish_interior(next, ExecutionSide::Host,
                            version(5), complete(26));

    ledger.publish_interior(current, ExecutionSide::Device,
                            version(2), complete(40));
    expect(ledger.inspect(current).interior.residency
               == StateResidency::DeviceValid,
           "Device write publishes Device-only interior");
    expect_throws([&] {
        ledger.require_readable(
            current, {ExecutionSide::Host, version(2), true, false});
    }, "Device write makes Host interior stale");
}

void test_transfers_and_explicit_wrappers()
{
    StateResidencyLedger ledger{{1}};
    TransferExecutionContext context;
    TestTransferExecutor executor{ledger, context};
    const auto first = handle(30, 1);
    const auto second = handle(31, 1);
    const StateKey current = key(first, StateSlot::Current);
    ledger.register_block(first, version(1), complete(10));
    ledger.register_block(second, version(1), complete(10));
    ledger.publish_ghost(current, ExecutionSide::Host,
                         version(1), complete(11));
    ledger.publish_ghost(key(second, StateSlot::Current), ExecutionSide::Host,
                         version(1), complete(11));

    executor.upload_slot(current, StateRegion::Interior, pending(20));
    const SlotCoherence pending_h2d = ledger.inspect(current);
    expect(pending_h2d.interior.residency == StateResidency::HostValid
               && pending_h2d.interior.pending_transfer
                      == PendingTransferPhase::PendingH2D
               && pending_h2d.interior.completion == pending(20),
           "H2D begin keeps Host source valid and records pending token");
    expect_throws([&] {
        ledger.publish_interior(current, ExecutionSide::Host,
                                version(2), complete(21));
    }, "pending H2D source cannot mutate");
    expect_throws([&] {
        ledger.publish_ghost(current, ExecutionSide::Host,
                             version(1), complete(21));
    }, "another region cannot mutate while slot transfer is pending");
    expect_throws([&] { ledger.quiesce(first); },
                  "pending H2D is not quiescent");
    expect_throws([&] {
        ledger.complete_upload_slot(current, StateRegion::Interior, pending(20));
    }, "pending token cannot publish synchronization");
    expect_throws([&] {
        ledger.complete_upload_slot(current, StateRegion::Interior, complete(21));
    }, "wrong completion token is rejected");
    ledger.complete_upload_slot(current, StateRegion::Interior, complete(20));
    expect(ledger.inspect(current).interior.residency
               == StateResidency::Synchronized,
           "exact H2D completion publishes Synchronized");

    ledger.publish_interior(current, ExecutionSide::Device,
                            version(2), complete(30));
    executor.materialize_host_current(first, StateRegion::Interior, pending(31));
    expect(ledger.inspect(current).interior.pending_transfer
               == PendingTransferPhase::PendingD2H,
           "materialize_host_current begins D2H");
    expect_throws([&] {
        ledger.publish_interior(current, ExecutionSide::Device,
                                version(3), complete(32));
    }, "pending D2H source cannot mutate");
    expect_throws([&] {
        ledger.complete_upload_slot(current, StateRegion::Interior, complete(31));
    }, "upload completion cannot finish D2H");
    ledger.complete_materialize_host_current(
        first, StateRegion::Interior, complete(31));
    expect(ledger.inspect(current).interior.residency
               == StateResidency::Synchronized,
           "exact D2H completion publishes Synchronized");

    ledger.publish_interior(current, ExecutionSide::Host,
                            version(3), complete(40));
    executor.upload_slot(current, StateRegion::Interior, pending(41));
    expect_throws([&] {
        ledger.complete_materialize_host_current(
            first, StateRegion::Interior, complete(41));
    }, "materialize completion cannot finish H2D");
    ledger.complete_upload_slot(current, StateRegion::Interior, complete(41));

    ledger.publish_interior(current, ExecutionSide::Host,
                            version(4), complete(50));
    ledger.publish_ghost(current, ExecutionSide::Host,
                         version(4), complete(51));
    const StateKey second_current = key(second, StateSlot::Current);
    executor.upload_slot(current, StateRegion::Interior, pending(60));
    executor.upload_slot(current, StateRegion::Ghost, pending(60));
    executor.upload_slot(second_current, StateRegion::Interior, pending(60));
    executor.upload_slot(second_current, StateRegion::Ghost, pending(60));
    ledger.complete_upload_slot(current, StateRegion::Interior, complete(60));
    ledger.complete_upload_slot(current, StateRegion::Ghost, complete(60));
    ledger.complete_upload_slot(second_current, StateRegion::Interior, complete(60));
    ledger.complete_upload_slot(second_current, StateRegion::Ghost, complete(60));
    expect(ledger.inspect(current).interior.residency
               == StateResidency::Synchronized
               && ledger.inspect(second_current).ghost.residency
                      == StateResidency::Synchronized,
           "one scheduler token fans out across keys and regions");

    ledger.publish_interior(current, ExecutionSide::Host,
                            version(5), complete(70));
    expect_throws([&] {
        ledger.publish_ghost(current, ExecutionSide::Host,
                             version(5), complete(60));
    }, "ghost invalidation preserves its private replay high-watermark");
    expect_throws([&] {
        executor.upload_slot(current, StateRegion::Interior, pending(60));
    }, "old same-region transfer token is rejected");
    executor.upload_slot(current, StateRegion::Interior, pending(71));
    ledger.complete_upload_slot(current, StateRegion::Interior, complete(71));
    ledger.publish_interior(current, ExecutionSide::Device,
                            version(6), complete(80));
    expect(ledger.inspect(current).interior.residency
               == StateResidency::DeviceValid,
           "Device update after synchronization invalidates Host copy");
    expect(context.explicit_transfer_calls == 9,
           "every explicit transfer attempt executes through the wrapper");
}

void test_quiesce_retirement_and_new_epoch()
{
    StateResidencyLedger ledger{{1}};
    TransferExecutionContext context;
    TestTransferExecutor executor{ledger, context};
    const auto block = handle(40, 1);
    const StateKey current = key(block, StateSlot::Current);
    ledger.register_block(block, version(1), complete(10));
    ledger.quiesce(block);
    ledger.quiesce();

    ledger.publish_ghost(current, ExecutionSide::Host,
                         version(1), complete(11));
    executor.upload_slot(current, StateRegion::Ghost, pending(20));
    expect_throws([&] {
        ledger.publish_interior(current, ExecutionSide::Host,
                                version(2), complete(21));
    }, "pending ghost blocks an interior mutation in the same slot");
    expect_throws([&] { ledger.retire_block(block); },
                  "pending block cannot retire");
    expect_throws([&] { ledger.quiesce(); },
                  "global quiesce detects a pending block");
    ledger.complete_upload_slot(current, StateRegion::Ghost, complete(20));
    ledger.quiesce(block);
    ledger.retire_block(block);

    expect_throws([&] { (void) ledger.inspect(current); },
                  "retired block cannot be inspected");
    expect_throws([&] {
        ledger.register_block(block, version(2), complete(30));
    }, "retired handle is a permanent same-epoch tombstone");

    StateResidencyLedger next_epoch{{2}};
    const auto rebound = handle(40, 2);
    next_epoch.register_block(rebound, version(2), complete(1));
    expect(next_epoch.inspect(key(rebound, StateSlot::Current)).interior.version
               == version(2),
           "new epoch ledger accepts correctly rebound surviving UID");
    expect_throws([&] {
        next_epoch.register_block(block, version(2), complete(2));
    }, "new epoch ledger rejects old-epoch handle");
    expect(context.explicit_transfer_calls == 1,
           "retirement test records its explicit transfer attempt");
}

void prepare_rotation_fixture(StateResidencyLedger& ledger,
                              amr::BlockHandle block)
{
    ledger.register_block(block, version(1), complete(100));
    ledger.publish_ghost(key(block, StateSlot::Current), ExecutionSide::Host,
                         version(1), complete(101));
    ledger.publish_interior(key(block, StateSlot::Next), ExecutionSide::Host,
                            version(2), complete(200));
    ledger.publish_ghost(key(block, StateSlot::Next), ExecutionSide::Host,
                         version(2), complete(201));
    ledger.publish_interior(key(block, StateSlot::Scratch), ExecutionSide::Host,
                            version(3), complete(300));
    ledger.publish_ghost(key(block, StateSlot::Scratch), ExecutionSide::Host,
                         version(3), complete(301));
}

void test_full_rotation_and_high_watermarks()
{
    {
        StateResidencyLedger ledger{{1}};
        const auto block = handle(50, 1);
        prepare_rotation_fixture(ledger, block);
        const SlotCoherence old_current = ledger.inspect(key(block, StateSlot::Current));
        const SlotCoherence old_next = ledger.inspect(key(block, StateSlot::Next));
        const SlotCoherence old_scratch = ledger.inspect(key(block, StateSlot::Scratch));

        ledger.rotate_slots(
            block, {StateSlot::Next, StateSlot::Scratch, StateSlot::Current});
        expect(same_slot(ledger.inspect(key(block, StateSlot::Current)), old_next)
                   && same_slot(ledger.inspect(key(block, StateSlot::Next)), old_scratch)
                   && same_slot(ledger.inspect(key(block, StateSlot::Scratch)), old_current),
               "rotation moves the complete logical SlotCoherence records");

        const StateKey current = key(block, StateSlot::Current);
        expect_throws([&] {
            ledger.publish_ghost(current, ExecutionSide::Host,
                                 version(2), complete(150));
        }, "destination 101/source 201 ghost HWM rejects intermediate token");
        expect_throws([&] {
            ledger.publish_ghost(current, ExecutionSide::Host,
                                 version(2), complete(201));
        }, "destination 101/source 201 ghost HWM rejects its equal token");
        ledger.publish_ghost(current, ExecutionSide::Host,
                             version(2), complete(202));
        expect_throws([&] {
            ledger.publish_interior(current, ExecutionSide::Host,
                                    version(3), complete(150));
        }, "destination 100/source 200 interior HWM becomes 200");
        expect_throws([&] {
            ledger.publish_interior(current, ExecutionSide::Host,
                                    version(3), complete(200));
        }, "destination 100/source 200 rejects its equal token");
        ledger.publish_interior(current, ExecutionSide::Host,
                                version(3), complete(203));
    }

    {
        StateResidencyLedger ledger{{1}};
        const auto block = handle(51, 1);
        prepare_rotation_fixture(ledger, block);
        const SlotCoherence old_next = ledger.inspect(key(block, StateSlot::Next));
        ledger.rotate_slots(
            block, {StateSlot::Current, StateSlot::Scratch, StateSlot::Next});
        const StateKey scratch = key(block, StateSlot::Scratch);
        expect(same_slot(ledger.inspect(scratch), old_next),
               "Scratch receives the complete former Next record");
        expect_throws([&] {
            ledger.publish_ghost(scratch, ExecutionSide::Host,
                                 version(2), complete(250));
        }, "destination 301/source 201 ghost HWM remains 301");
        expect_throws([&] {
            ledger.publish_ghost(scratch, ExecutionSide::Host,
                                 version(2), complete(301));
        }, "destination 301/source 201 ghost rejects its equal token");
        ledger.publish_ghost(scratch, ExecutionSide::Host,
                             version(2), complete(302));
        expect_throws([&] {
            ledger.publish_interior(scratch, ExecutionSide::Host,
                                    version(3), complete(250));
        }, "destination 300/source 200 interior HWM remains 300");
        expect_throws([&] {
            ledger.publish_interior(scratch, ExecutionSide::Host,
                                    version(3), complete(300));
        }, "destination 300/source 200 interior rejects its equal token");
        ledger.publish_interior(scratch, ExecutionSide::Host,
                                version(3), complete(303));
    }

    {
        StateResidencyLedger ledger{{1}};
        TransferExecutionContext context;
        TestTransferExecutor executor{ledger, context};
        const auto block = handle(52, 1);
        prepare_rotation_fixture(ledger, block);
        expect_throws([&] {
            ledger.rotate_slots(
                block, {StateSlot::Current, StateSlot::Current, StateSlot::Scratch});
        }, "rotation requires a true slot permutation");
        ledger.publish_interior(key(block, StateSlot::Current), ExecutionSide::Host,
                                version(4), complete(400));
        executor.upload_slot(key(block, StateSlot::Current),
                             StateRegion::Interior, pending(401));
        expect_throws([&] {
            ledger.rotate_slots(
                block, {StateSlot::Next, StateSlot::Scratch, StateSlot::Current});
        }, "pending slot cannot rotate");
        ledger.complete_upload_slot(
            key(block, StateSlot::Current), StateRegion::Interior, complete(401));
        expect(context.explicit_transfer_calls == 1,
               "rotation test records its explicit transfer attempt");
    }
}

void test_cpu_executor_contract_witness()
{
    StateResidencyLedger ledger{{1}};
    TransferExecutionContext context{7};
    context.reset();
    TestTransferExecutor executor{ledger, context};
    (void) executor;
    const auto block = handle(60, 1);
    const StateKey current = key(block, StateSlot::Current);
    const StateKey next = key(block, StateSlot::Next);
    ledger.register_block(block, version(1), complete(10));
    ledger.publish_ghost(current, ExecutionSide::Host,
                         version(1), complete(11));

    const double input_bytes = 3.5;
    double output_bytes = -999.0;
    ledger.require_readable(
        current, {ExecutionSide::Host, version(1), true, true});
    output_bytes = input_bytes * 2.0;
    ledger.publish_interior(next, ExecutionSide::Host,
                            version(2), complete(20));
    ledger.publish_ghost(next, ExecutionSide::Host,
                         version(2), complete(21));
    ledger.rotate_slots(
        block, {StateSlot::Next, StateSlot::Scratch, StateSlot::Current});
    ledger.require_readable(
        current, {ExecutionSide::Host, version(2), true, true});

    expect(output_bytes == 7.0, "test-local CPU stage writes expected bytes");
    expect(context.explicit_transfer_calls == 0,
           "CPU stage calls no explicit materialize/upload seam");
    for (const SlotCoherence& state : snapshot_all(ledger, block)) {
        for (const RegionCoherence* region : {&state.interior, &state.ghost}) {
            expect(region->residency == StateResidency::Invalid
                       || region->residency == StateResidency::HostValid,
                   "CPU witness creates no DeviceValid or Synchronized state");
        }
    }
}

} // namespace

int main()
{
    run_case("value contract and helpers", test_value_contract_and_helpers);
    run_case("registration snapshots and reads", test_registration_snapshots_and_reads);
    run_case("publication and region versions", test_publication_and_region_versions);
    run_case("transfers and explicit wrappers", test_transfers_and_explicit_wrappers);
    run_case("quiesce retirement and new epoch", test_quiesce_retirement_and_new_epoch);
    run_case("full rotation and high watermarks", test_full_rotation_and_high_watermarks);
    run_case("CPU executor contract witness", test_cpu_executor_contract_witness);

    if (failures != 0) {
        std::cerr << failures << " state residency assertion(s) failed\n";
        return 1;
    }
    std::cout << "state residency contract passed\n";
    return 0;
}
