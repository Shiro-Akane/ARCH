/**
 * @file test_shared_stage_scheduler.cpp
 * @brief Verify shared stage sequencing, visibility and state publication.
 *
 * Check Euler/RK/RKL schedules, topology epochs, boundary completion and
 * rollback with both contract fixtures and production driver wiring.
 */
#include "driver/StageScheduler.h"
#include "driver/TopologyIdentityRegistry.h"

#include <cstdlib>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#ifndef ARCH_SOURCE_DIR
#error "ARCH_SOURCE_DIR must identify the production source tree"
#endif

namespace allocation_failure_fixture {

bool fail_next = false;

void arm() noexcept
{
    fail_next = true;
}

bool armed() noexcept
{
    return fail_next;
}

void disarm() noexcept
{
    fail_next = false;
}

} // namespace allocation_failure_fixture

void* operator new(std::size_t size)
{
    if (allocation_failure_fixture::fail_next) {
        allocation_failure_fixture::fail_next = false;
        throw std::bad_alloc{};
    }
    if (void* allocation = std::malloc(size)) return allocation;
    throw std::bad_alloc{};
}

void operator delete(void* allocation) noexcept
{
    std::free(allocation);
}

void operator delete(void* allocation, std::size_t) noexcept
{
    std::free(allocation);
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete[](void* allocation) noexcept
{
    std::free(allocation);
}

void operator delete[](void* allocation, std::size_t) noexcept
{
    std::free(allocation);
}

namespace {

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template <typename Exception, typename Callable>
void expect_throws(Callable&& callable, const char* message)
{
    try {
        callable();
    } catch (const Exception&) {
        return;
    } catch (...) {
        std::cerr << "FAIL: wrong exception: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
    std::cerr << "FAIL: exception not thrown: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

template <typename Exception, typename Callable>
bool throws_exactly(Callable&& callable)
{
    try {
        callable();
    } catch (const Exception&) {
        return true;
    } catch (...) {
    }
    return false;
}

using arch::topology::LogicalBlockIdentity;
using arch::topology::TopologyDomainBounds;
using arch::topology::TopologyIdentityRegistry;
using arch::topology::TopologyObservation;
using arch::topology::TopologyReconciliation;

LogicalBlockIdentity key(int dimension, int level, std::uint32_t x1,
                         std::uint32_t x2 = 0, std::uint32_t x3 = 0)
{
    return {dimension, level, x1, x2, x3};
}

TopologyObservation observation(int pool_index, LogicalBlockIdentity identity)
{
    return {pool_index, identity};
}

TopologyReconciliation adopt_topology(
    TopologyIdentityRegistry& registry,
    const std::vector<TopologyObservation>& observations)
{
    return registry.commit_after_success(
        registry.stage_adoption(observations), [](const auto&) {});
}

TopologyReconciliation reconcile_topology(
    TopologyIdentityRegistry& registry,
    const std::vector<TopologyObservation>& observations)
{
    return registry.commit_after_success(
        registry.stage_reconciliation(observations), [](const auto&) {});
}

void test_identity_first_reconciliation()
{
    TopologyIdentityRegistry registry({2, {2, 2, 1}, 3});
    const LogicalBlockIdentity a = key(2, 0, 0, 0);
    const LogicalBlockIdentity b = key(2, 0, 1, 0);
    const std::vector initial{observation(1, a), observation(2, b)};
    const auto adopted = adopt_topology(registry, initial);
    expect(adopted.initial_adoption, "initial adoption is explicit");
    expect(!adopted.topology_changed, "initial adoption is not a regrid");
    expect(adopted.epoch == amr::TopologyEpoch{1}, "initial epoch");
    const amr::BlockHandle a_initial = registry.handle_for(a);
    const amr::BlockHandle b_initial = registry.handle_for(b);

    const std::vector swapped{observation(2, a), observation(1, b)};
    const auto swap_result = reconcile_topology(registry, swapped);
    expect(!swap_result.topology_changed, "pool swap is not topology change");
    expect(registry.handle_for(a) == a_initial, "pool swap preserves A");
    expect(registry.handle_for(b) == b_initial, "pool swap preserves B");
    expect(registry.handle_for_pool(2) == a_initial,
           "lowering built after identity match for A");
    expect(registry.handle_for_pool(1) == b_initial,
           "lowering built after identity match for B");

    const LogicalBlockIdentity c = key(2, 0, 0, 1);
    const std::vector moved_and_reused{
        observation(3, a), observation(2, b), observation(1, c)};
    const auto changed = reconcile_topology(registry, moved_and_reused);
    expect(changed.topology_changed, "new logical identity changes topology");
    expect(changed.epoch == amr::TopologyEpoch{2}, "changed epoch once");
    expect(registry.handle_for(a).uid == a_initial.uid,
           "survivor move preserves A uid");
    expect(registry.handle_for(b).uid == b_initial.uid,
           "survivor preserves B uid");
    expect(registry.handle_for(c).uid != a_initial.uid
               && registry.handle_for(c).uid != b_initial.uid,
           "old pool reuse issues C uid");
    expect(registry.handle_for_pool(1) == registry.handle_for(c),
           "old pool lowers to new identity only after reconcile");

    const amr::BlockUid c_uid = registry.handle_for(c).uid;
    const std::vector removed_c{observation(8, a), observation(9, b)};
    const auto removed = reconcile_topology(registry, removed_c);
    expect(removed.topology_changed, "identity disappearance changes topology");
    expect(registry.is_retired(c_uid), "disappeared identity uid retired");

    const LogicalBlockIdentity d = key(2, 0, 1, 1);
    reconcile_topology(registry,
                       std::vector{observation(8, a), observation(9, b),
                                   observation(1, d)});
    expect(registry.handle_for(d).uid != c_uid,
           "retired uid is never resurrected on pool reuse");
}

void test_permutation_and_pool_move_preserve_epoch()
{
    TopologyIdentityRegistry registry({1, {3, 1, 1}, 2});
    const auto a = key(1, 0, 0);
    const auto b = key(1, 0, 1);
    const auto c = key(1, 0, 2);
    adopt_topology(registry,
                   std::vector{observation(4, a), observation(5, b),
                               observation(6, c)});
    const auto before_epoch = registry.epoch();
    const auto before_a = registry.handle_for(a);
    const auto before_b = registry.handle_for(b);
    const auto before_c = registry.handle_for(c);
    const auto result = reconcile_topology(
        registry, std::vector{observation(40, c), observation(41, a),
                              observation(42, b)});
    expect(!result.topology_changed, "permutation and relocation unchanged");
    expect(registry.epoch() == before_epoch, "unchanged key set keeps epoch");
    expect(registry.handle_for(a) == before_a, "relocation preserves A handle");
    expect(registry.handle_for(b) == before_b, "relocation preserves B handle");
    expect(registry.handle_for(c) == before_c, "relocation preserves C handle");
}

void test_domain_bounds_and_global_roots()
{
    TopologyIdentityRegistry registry({2, {2, 2, 1}, 2});
    const auto root0_child0 = key(2, 1, 0, 0);
    const auto root1_child0 = key(2, 1, 2, 0);
    adopt_topology(registry,
                   std::vector{observation(1, root0_child0),
                               observation(2, root1_child0)});
    expect(registry.handle_for(root0_child0).uid
               != registry.handle_for(root1_child0).uid,
           "two roots with equal local child coordinates are globally unique");

    const auto invalid_adopt = [](TopologyDomainBounds bounds,
                                  LogicalBlockIdentity identity) {
        TopologyIdentityRegistry invalid(bounds);
        adopt_topology(invalid, std::vector{observation(1, identity)});
    };
    expect_throws<std::invalid_argument>(
        [&] { invalid_adopt({2, {2, 2, 1}, 2}, key(1, 0, 0)); },
        "identity dimension must match domain");
    expect_throws<std::invalid_argument>(
        [&] { TopologyIdentityRegistry invalid({2, {2, 0, 1}, 2}); },
        "active root extent must be positive");
    expect_throws<std::invalid_argument>(
        [&] { invalid_adopt({2, {2, 2, 1}, 2}, key(2, 3, 0, 0)); },
        "level bounded by configuration");
    expect_throws<std::invalid_argument>(
        [&] { invalid_adopt({2, {2, 2, 1}, 2}, key(2, 1, 4, 0)); },
        "global x1 bounded by level-scaled roots");
    expect_throws<std::invalid_argument>(
        [&] { invalid_adopt({2, {2, 2, 1}, 2}, key(2, 1, 0, 4)); },
        "global x2 bounded by level-scaled roots");
    expect_throws<std::invalid_argument>(
        [&] { invalid_adopt({2, {2, 2, 1}, 2}, key(2, 1, 0, 0, 1)); },
        "inactive x3 is canonical zero");
    expect_throws<std::invalid_argument>(
        [&] { invalid_adopt({1, {2, 1, 1}, 2}, key(1, 0, 0, 1)); },
        "inactive x2 is canonical zero");
}

void test_snapshot_validation_precedes_lowering()
{
    TopologyIdentityRegistry registry({1, {2, 1, 1}, 2});
    const auto a = key(1, 0, 0);
    const auto b = key(1, 0, 1);
    adopt_topology(registry,
                   std::vector{observation(10, a), observation(11, b)});
    const auto old_a = registry.handle_for_pool(10);
    const auto old_b = registry.handle_for_pool(11);

    expect_throws<std::invalid_argument>(
        [&] {
            (void)registry.stage_reconciliation(
                std::vector{observation(20, a), observation(20, b)});
        },
        "duplicate pool rejected");
    expect(registry.handle_for_pool(10) == old_a
               && registry.handle_for_pool(11) == old_b,
           "failed lowering leaves prior lowering unchanged");

    expect_throws<std::invalid_argument>(
        [&] {
            (void)registry.stage_reconciliation(
                std::vector{observation(20, a), observation(21, a)});
        },
        "duplicate logical key rejected");
    expect(registry.handle_for_pool(10) == old_a
               && registry.handle_for_pool(11) == old_b,
           "failed identity reconcile leaves prior lowering unchanged");
}

void test_staged_topology_visibility_and_snapshot_completeness()
{
    TopologyIdentityRegistry registry({1, {3, 1, 1}, 2});
    const auto a = key(1, 0, 0);
    const auto b = key(1, 0, 1);
    const auto c = key(1, 0, 2);

    auto initial = registry.stage_adoption(
        std::vector{observation(10, a), observation(11, b)});
    expect_throws<std::logic_error>(
        [&] { (void)registry.epoch(); },
        "staged adoption is invisible before commit");
    const auto adopted = registry.commit_after_success(
        std::move(initial), [](const auto&) {});
    expect(adopted.epoch == amr::TopologyEpoch{1},
           "successful adoption becomes visible");
    const auto old_a = registry.handle_for(a);
    const auto old_b = registry.handle_for(b);
    registry.validate_committed_snapshot(
        std::vector{observation(11, b), observation(10, a)});

    for (const auto& incomplete : {
             std::vector{observation(10, a)},
             std::vector{observation(10, a), observation(11, b),
                         observation(12, c)},
             std::vector{observation(10, b), observation(11, a)}}) {
        expect_throws<std::invalid_argument>(
            [&] { registry.validate_committed_snapshot(incomplete); },
            "pre-regrid snapshot must be complete and bijective");
        expect(registry.handle_for(a) == old_a
                   && registry.handle_for(b) == old_b
                   && registry.handle_for_pool(10) == old_a
                   && registry.handle_for_pool(11) == old_b,
               "failed pre-regrid validation changes no authority");
    }
    expect_throws<std::invalid_argument>(
        [&] {
            registry.validate_committed_snapshot(
                std::vector{observation(10, a), observation(10, b)});
        },
        "duplicate lowering rejected before regrid");

    auto failed_candidate = registry.stage_reconciliation(
        std::vector{observation(20, a), observation(12, c)});
    const auto failed_view = failed_candidate.reconciliation();
    expect(failed_view.topology_changed,
           "changed topology is represented by a staged candidate");
    expect(registry.epoch() == amr::TopologyEpoch{1}
               && registry.handle_for(a) == old_a
               && registry.handle_for(b) == old_b
               && registry.handle_for_pool(10) == old_a,
           "candidate does not publish epoch handles or lowering");
    expect_throws<std::runtime_error>(
        [&] {
            (void)registry.commit_after_success(
                std::move(failed_candidate), [](const auto&) {
                    throw std::runtime_error("registration/boundary failure");
                });
        },
        "failed registration/boundary prevents topology commit");
    expect(registry.epoch() == amr::TopologyEpoch{1}
               && registry.handle_for(a) == old_a
               && registry.handle_for(b) == old_b
               && registry.handle_for_pool(10) == old_a,
           "failed candidate remains completely invisible");

    auto successful_candidate = registry.stage_reconciliation(
        std::vector{observation(20, a), observation(12, c)});
    bool finalized = false;
    const auto changed = registry.commit_after_success(
        std::move(successful_candidate), [&](const auto& staged) {
            expect(registry.epoch() == amr::TopologyEpoch{1},
                   "finalizer runs before public commit");
            expect(staged.epoch == amr::TopologyEpoch{2},
                   "finalizer receives proposed epoch");
            finalized = true;
        });
    expect(finalized && changed.epoch == amr::TopologyEpoch{2},
           "successful finalizer permits one commit");
    expect(registry.handle_for(a).uid == old_a.uid,
           "successful commit preserves survivor uid");
    expect(registry.handle_for(a).epoch == changed.epoch,
           "successful commit rebinds survivor epoch");
    expect(registry.handle_for(c).uid != old_a.uid
               && registry.handle_for(c).uid != old_b.uid,
           "successful commit issues a new uid");
    expect(registry.handle_for_pool(20) == registry.handle_for(a),
           "successful commit publishes survivor lowering");
    expect(registry.handle_for_pool(12) == registry.handle_for(c),
           "successful commit publishes new-block lowering");
}

void test_candidate_transaction_safety()
{
    const auto a = key(1, 0, 0);
    const auto b = key(1, 0, 1);
    const std::vector initial{observation(1, a), observation(2, b)};
    bool stale_ok = false;
    bool consumed_ok = false;
    bool moved_from_ok = false;
    bool prepare_failure_ok = false;
    bool publication_noalloc_ok = false;
    bool reentrant_ok = false;
    bool success_once_ok = false;

    {
        TopologyIdentityRegistry registry({1, {2, 1, 1}, 2});
        auto accepted = registry.stage_adoption(initial);
        auto stale = registry.stage_adoption(initial);
        int accepted_calls = 0;
        (void)registry.commit_after_success(
            std::move(accepted),
            [&](const auto&) { ++accepted_calls; });
        const auto committed_a = registry.handle_for(a);

        int stale_calls = 0;
        const bool stale_rejected = throws_exactly<std::logic_error>([&] {
            (void)registry.commit_after_success(
                std::move(stale), [&](const auto&) { ++stale_calls; });
        });
        stale_ok = stale_rejected && stale_calls == 0
                   && registry.handle_for(a) == committed_a;

        int consumed_calls = 0;
        const bool consumed_rejected = throws_exactly<std::logic_error>([&] {
            (void)registry.commit_after_success(
                std::move(accepted),
                [&](const auto&) { ++consumed_calls; });
        });
        consumed_ok = consumed_rejected && consumed_calls == 0
                      && registry.handle_for(a) == committed_a;
        success_once_ok = accepted_calls == 1;
    }

    {
        TopologyIdentityRegistry registry({1, {2, 1, 1}, 2});
        auto source = registry.stage_adoption(initial);
        auto destination = std::move(source);
        int source_calls = 0;
        const bool source_rejected = throws_exactly<std::logic_error>([&] {
            (void)registry.commit_after_success(
                std::move(source),
                [&](const auto&) { ++source_calls; });
        });
        const bool still_unadopted = throws_exactly<std::logic_error>(
            [&] { (void)registry.epoch(); });
        int destination_calls = 0;
        const bool destination_succeeded =
            !throws_exactly<std::logic_error>([&] {
                (void)registry.commit_after_success(
                    std::move(destination),
                    [&](const auto&) { ++destination_calls; });
            });
        const bool destination_published =
            !throws_exactly<std::invalid_argument>(
                [&] { (void)registry.handle_for(a); });
        moved_from_ok = source_rejected && source_calls == 0
                        && still_unadopted && destination_succeeded
                        && destination_calls == 1
                        && destination_published;
    }

    {
        TopologyIdentityRegistry registry({1, {2, 1, 1}, 2});
        auto candidate = registry.stage_adoption(initial);
        int finalizer_calls = 0;
        allocation_failure_fixture::arm();
        const bool allocation_rejected = throws_exactly<std::bad_alloc>([&] {
            (void)registry.commit_after_success(
                std::move(candidate),
                [&](const auto&) { ++finalizer_calls; });
        });
        const bool still_unadopted = throws_exactly<std::logic_error>(
            [&] { (void)registry.epoch(); });
        (void)registry.commit_after_success(
            std::move(candidate),
            [&](const auto&) { ++finalizer_calls; });
        prepare_failure_ok = allocation_rejected && still_unadopted
                             && finalizer_calls == 1
                             && registry.handle_for(a).uid.value != 0;
    }

    {
        TopologyIdentityRegistry registry({1, {2, 1, 1}, 2});
        auto candidate = registry.stage_adoption(initial);
        int finalizer_calls = 0;
        const bool commit_succeeded = !throws_exactly<std::bad_alloc>([&] {
            (void)registry.commit_after_success(
                std::move(candidate), [&](const auto&) {
                    ++finalizer_calls;
                    allocation_failure_fixture::arm();
                });
        });
        const bool allocation_remained_armed =
            allocation_failure_fixture::armed();
        allocation_failure_fixture::disarm();
        const bool handle_published =
            !throws_exactly<std::invalid_argument>(
                [&] { (void)registry.handle_for(a); });
        publication_noalloc_ok = commit_succeeded
                                 && allocation_remained_armed
                                 && finalizer_calls == 1 && handle_published;
    }

    {
        TopologyIdentityRegistry registry({1, {2, 1, 1}, 2});
        (void)adopt_topology(registry, initial);
        const auto committed_a = registry.handle_for(a);
        auto outer = registry.stage_reconciliation(
            std::vector{observation(3, a), observation(2, b)});
        auto inner = registry.stage_reconciliation(
            std::vector{observation(4, a), observation(2, b)});
        int outer_calls = 0;
        int inner_calls = 0;
        bool inner_rejected = false;
        const bool outer_succeeded = !throws_exactly<std::logic_error>([&] {
            (void)registry.commit_after_success(
                std::move(outer), [&](const auto&) {
                    ++outer_calls;
                    inner_rejected = throws_exactly<std::logic_error>([&] {
                        (void)registry.commit_after_success(
                            std::move(inner),
                            [&](const auto&) { ++inner_calls; });
                    });
                });
        });
        const bool old_pool_rejected = throws_exactly<std::invalid_argument>(
            [&] { (void)registry.handle_for_pool(1); });
        reentrant_ok = outer_succeeded && inner_rejected
                       && outer_calls == 1 && inner_calls == 0
                       && registry.handle_for_pool(3) == committed_a
                       && old_pool_rejected;
    }

    if (!(stale_ok && consumed_ok && moved_from_ok && prepare_failure_ok
          && publication_noalloc_ok && reentrant_ok && success_once_ok)) {
        std::cerr << "candidate transaction flags: stale=" << stale_ok
                  << " consumed=" << consumed_ok
                  << " moved_from=" << moved_from_ok
                  << " prepare_failure=" << prepare_failure_ok
                  << " publication_noalloc=" << publication_noalloc_ok
                  << " reentrant=" << reentrant_ok
                  << " success_once=" << success_once_ok << '\n';
    }
    expect(stale_ok && consumed_ok && moved_from_ok && prepare_failure_ok
               && publication_noalloc_ok && reentrant_ok && success_once_ok,
           "candidate transaction validates and prepares before finalizer");
}

using arch::state::CompletionState;
using arch::state::CompletionToken;
using arch::state::ExecutionSide;
using arch::state::SlotCoherence;
using arch::state::StateKey;
using arch::state::StateResidency;
using arch::state::StateResidencyLedger;
using arch::state::StateSlot;
using arch::state::StateVersion;

void expect_slot_equal(const SlotCoherence& actual,
                       const SlotCoherence& expected,
                       const char* message)
{
    const bool equal =
        actual.interior.residency == expected.interior.residency
        && actual.interior.version == expected.interior.version
        && actual.interior.completion == expected.interior.completion
        && actual.interior.pending_transfer
               == expected.interior.pending_transfer
        && actual.ghost.residency == expected.ghost.residency
        && actual.ghost.version == expected.ghost.version
        && actual.ghost.completion == expected.ghost.completion
        && actual.ghost.pending_transfer == expected.ghost.pending_transfer
        && actual.ghost_source_version == expected.ghost_source_version;
    expect(equal, message);
}

void test_hydro_and_rkl_descriptor_fingerprints()
{
    using namespace arch::scheduler;
    const HydroPlan euler = make_hydro_plan(HydroMethod::Euler);
    expect(euler.stages.size() == 1, "Euler stage count");
    expect(euler.stages[0].old_slot == StateSlot::Current
               && euler.stages[0].input_slot == StateSlot::Current
               && euler.stages[0].output_slot == StateSlot::Next,
           "Euler slot route");
    expect(euler.stages[0].old_weight == 0.0
               && euler.stages[0].update_weight == 1.0
               && euler.stages[0].flux_register_weight == 1.0,
           "Euler weights");
    expect(euler.final_rotation.current_from == StateSlot::Next
               && euler.final_rotation.next_from == StateSlot::Current
               && euler.final_rotation.scratch_from == StateSlot::Scratch,
           "Euler rotation");
    expect(euler.final_reflux_after_rotation, "Euler final reflux point");

    const HydroPlan rk2 = make_hydro_plan(HydroMethod::RK2);
    expect(rk2.stages.size() == 2, "RK2 stage count");
    expect(rk2.stages[0].output_slot == StateSlot::Scratch
               && rk2.stages[0].refresh_ghost_after
               && rk2.stages[0].flux_register_weight == 0.5,
           "RK2 stage 1 fingerprint");
    expect(rk2.stages[1].input_slot == StateSlot::Scratch
               && rk2.stages[1].output_slot == StateSlot::Next
               && rk2.stages[1].old_weight == 0.5
               && rk2.stages[1].update_weight == 0.5
               && !rk2.stages[1].refresh_ghost_after,
           "RK2 stage 2 fingerprint");

    const HydroPlan rk3 = make_hydro_plan(HydroMethod::RK3);
    expect(rk3.stages.size() == 3, "RK3 stage count");
    expect(rk3.stages[0].flux_register_weight == 1.0 / 6.0
               && rk3.stages[0].stage == 1
               && rk3.stages[0].input_slot == StateSlot::Current
               && rk3.stages[0].output_slot == StateSlot::Scratch
               && rk3.stages[1].stage == 2
               && rk3.stages[1].input_slot == StateSlot::Scratch
               && rk3.stages[1].output_slot == StateSlot::Next
               && rk3.stages[1].old_weight == 3.0 / 4.0
               && rk3.stages[1].update_weight == 1.0 / 4.0
               && rk3.stages[2].stage == 3
               && rk3.stages[2].input_slot == StateSlot::Next
               && rk3.stages[2].output_slot == StateSlot::Scratch
               && rk3.stages[2].old_weight == 1.0 / 3.0
               && rk3.stages[2].update_weight == 2.0 / 3.0,
           "RK3 stage fingerprints");
    expect(rk3.final_rotation.current_from == StateSlot::Scratch
               && rk3.final_rotation.next_from == StateSlot::Next
               && rk3.final_rotation.scratch_from == StateSlot::Current,
           "RK3 rotation");

    const RklPlan rkl1 = make_rkl_plan(RklMethod::RKL1, 4);
    expect(rkl1.stages.size() == 4, "RKL1 stage count");
    expect(!rkl1.second_order, "RKL1 no initial operator term");
    expect(rkl1.stages[0].previous_slot == StateSlot::Current
               && rkl1.stages[0].output_slot == StateSlot::Scratch,
           "RKL first stage route");
    expect(rkl1.stages[1].previous_slot == StateSlot::Scratch
               && rkl1.stages[1].older_slot == StateSlot::Next
               && rkl1.stages[1].output_slot == StateSlot::Next,
           "RKL even route");
    expect(rkl1.stages[2].previous_slot == StateSlot::Next
               && rkl1.stages[2].older_slot == StateSlot::Scratch
               && rkl1.stages[2].output_slot == StateSlot::Scratch,
           "RKL odd route");
    for (const auto& stage : rkl1.stages)
        expect(stage.reflux_before_publish && stage.refresh_ghost_after,
               "RKL per-stage reflux and ghost hook");
    expect(rkl1.final_rotation.current_from == StateSlot::Next
               && rkl1.final_rotation.next_from == StateSlot::Current,
           "even RKL rotation");

    const RklPlan rkl2 = make_rkl_plan(RklMethod::RKL2, 3);
    expect(rkl2.second_order, "RKL2 initial operator term");
    expect(rkl2.final_rotation.current_from == StateSlot::Scratch
               && rkl2.final_rotation.scratch_from == StateSlot::Current,
           "odd RKL rotation");
}

struct ReadyLedger {
    amr::BlockHandle handle{{1}, {1}};
    StateResidencyLedger ledger{{1}};
    arch::scheduler::MonotonicSchedulerClock clock{2, 1};

    explicit ReadyLedger(ExecutionSide side)
    {
        ledger.register_block(handle, {1}, {1, CompletionState::Complete});
        if (side == ExecutionSide::Host) {
            ledger.publish_ghost({handle, StateSlot::Current}, side, {1},
                                 {2, CompletionState::Complete});
        } else {
            ledger.publish_interior({handle, StateSlot::Current}, side, {2},
                                    {2, CompletionState::Complete});
            ledger.publish_ghost({handle, StateSlot::Current}, side, {2},
                                 {3, CompletionState::Complete});
            clock = arch::scheduler::MonotonicSchedulerClock{3, 2};
        }
    }
};

void test_execution_side_and_failure_publication_order()
{
    using namespace arch::scheduler;
    const auto descriptor = make_hydro_plan(HydroMethod::Euler).stages[0];

    ReadyLedger host(ExecutionSide::Host);
    StageExecutionContext host_context{
        ExecutionSide::Host, host.ledger, host.clock};
    const std::vector handles{host.handle};
    const auto before_current = host.ledger.inspect(
        {host.handle, StateSlot::Current});
    const auto before_next = host.ledger.inspect({host.handle, StateSlot::Next});
    const auto before_scratch = host.ledger.inspect(
        {host.handle, StateSlot::Scratch});
    expect_throws<std::runtime_error>(
        [&] {
            execute_stage(
                host_context, handles, descriptor,
                [](const auto&, CompletionToken) -> CompletionToken {
                    throw std::runtime_error("executor failure");
                },
                [](StateSlot, StateVersion, CompletionToken token) {
                    return token;
                });
        },
        "executor failure propagates");
    expect_slot_equal(host.ledger.inspect({host.handle, StateSlot::Current}),
                      before_current, "failure preserves Current");
    expect_slot_equal(host.ledger.inspect({host.handle, StateSlot::Next}),
                      before_next, "failure preserves Next");
    expect_slot_equal(host.ledger.inspect({host.handle, StateSlot::Scratch}),
                      before_scratch, "failure preserves Scratch");

    expect_throws<std::logic_error>(
        [&] {
            execute_stage(
                host_context, handles, descriptor,
                [](const auto&, CompletionToken token) {
                    return CompletionToken{token.value + 1,
                                           CompletionState::Complete};
                },
                [](StateSlot, StateVersion, CompletionToken token) {
                    return token;
                });
        },
        "wrong completion token rejected");
    expect_slot_equal(host.ledger.inspect({host.handle, StateSlot::Next}),
                      before_next, "wrong token publishes nothing");

    ReadyLedger device(ExecutionSide::Device);
    StageExecutionContext device_context{
        ExecutionSide::Device, device.ledger, device.clock};
    const auto result = execute_stage(
        device_context, std::vector{device.handle}, descriptor,
        [](const auto&, CompletionToken token) { return token; },
        [](StateSlot, StateVersion, CompletionToken token) { return token; });
    const auto output = device.ledger.inspect({device.handle, StateSlot::Next});
    expect(output.interior.residency == StateResidency::DeviceValid,
           "fake CUDA publishes DeviceValid");
    expect(output.interior.version == result.version,
           "fake CUDA publishes scheduler version");
    expect(output.ghost.residency == StateResidency::Invalid,
           "interior publication invalidates ghost");
}

void test_boundary_completion_and_full_rotation()
{
    using namespace arch::scheduler;
    ReadyLedger ready(ExecutionSide::Host);
    StageExecutionContext context{ExecutionSide::Host, ready.ledger,
                                  ready.clock};
    const std::vector handles{ready.handle};
    const auto rk2 = make_hydro_plan(HydroMethod::RK2);
    int boundary_calls = 0;
    const auto stage = execute_stage(
        context, handles, rk2.stages[0],
        [](const auto&, CompletionToken token) { return token; },
        [&](StateSlot slot, StateVersion, CompletionToken token) {
            expect(slot == StateSlot::Scratch, "boundary receives output slot");
            ++boundary_calls;
            return token;
        });
    expect(boundary_calls == 1, "RK2 stage 1 boundary count");
    const auto scratch = ready.ledger.inspect({ready.handle, StateSlot::Scratch});
    expect(scratch.ghost.residency == StateResidency::HostValid
               && scratch.ghost_source_version == stage.version,
           "boundary publishes matching ghost");

    const auto stage2 = execute_stage(
        context, handles, rk2.stages[1],
        [](const auto&, CompletionToken token) { return token; },
        [](StateSlot, StateVersion, CompletionToken token) { return token; });
    const auto next = ready.ledger.inspect({ready.handle, StateSlot::Next});
    expect(next.interior.version == stage2.version,
           "RK2 stage 2 publishes Next");

    bool physical_rotated = false;
    rotate_slots(
        context, handles, rk2.final_rotation,
        [&] { physical_rotated = true; });
    expect(physical_rotated, "physical rotation callback invoked");
    const auto current = ready.ledger.inspect({ready.handle, StateSlot::Current});
    expect_slot_equal(current, next, "full Next coherence rotated");
}

void test_full_logical_slot_copy()
{
    using namespace arch::scheduler;
    ReadyLedger ready(ExecutionSide::Host);
    StageExecutionContext context{ExecutionSide::Host, ready.ledger,
                                  ready.clock};
    const std::vector handles{ready.handle};
    bool physical_copy = false;
    const auto copied = copy_slot(
        context, handles, StateSlot::Current, StateSlot::Next,
        [&] { physical_copy = true; });
    expect(physical_copy, "logical copy pairs one physical copy");
    const auto destination =
        ready.ledger.inspect({ready.handle, StateSlot::Next});
    expect(destination.interior.residency == StateResidency::HostValid
               && destination.interior.version == StateVersion{1}
               && destination.ghost.residency == StateResidency::HostValid
               && destination.ghost.version == StateVersion{1}
               && destination.ghost_source_version == StateVersion{1},
           "logical copy publishes full interior and ghost validity");
    expect(copied.version == StateVersion{1},
           "logical copy preserves source version");
}

void test_fake_cuda_rkl_uses_shared_descriptors()
{
    using namespace arch::scheduler;
    ReadyLedger device(ExecutionSide::Device);
    StageExecutionContext context{ExecutionSide::Device, device.ledger,
                                  device.clock};
    const std::vector handles{device.handle};
    (void)copy_slot(context, handles, StateSlot::Current,
                    StateSlot::Scratch, [] {});
    (void)copy_slot(context, handles, StateSlot::Current,
                    StateSlot::Next, [] {});

    const RklPlan plan = make_rkl_plan(RklMethod::RKL1, 2);
    std::vector<int> executed;
    for (const RklStageDescriptor& stage : plan.stages) {
        (void)execute_rkl_stage(
            context, handles, stage,
            [&](const RklStageDescriptor& descriptor,
                CompletionToken token) {
                executed.push_back(descriptor.stage);
                return token;
            },
            [](StateSlot, StateVersion, CompletionToken token) {
                return token;
            });
    }
    rotate_slots(context, handles, plan.final_rotation, [] {});
    const auto current =
        device.ledger.inspect({device.handle, StateSlot::Current});
    expect(executed == std::vector<int>({1, 2}),
           "fake CUDA executes exact shared RKL order");
    expect(current.interior.residency == StateResidency::DeviceValid
               && current.ghost.residency == StateResidency::DeviceValid
               && current.ghost_source_version == current.interior.version,
           "fake CUDA RKL remains Device-readable after full rotation");
}

void test_scheduler_owned_reflux_and_runtime_lane_traces()
{
    using namespace arch::scheduler;
    using TracePair = std::pair<std::vector<std::string>,
                                std::vector<std::string>>;
    const auto slot_name = [](StateSlot slot) -> const char* {
        switch (slot) {
        case StateSlot::Current: return "Current";
        case StateSlot::Next: return "Next";
        case StateSlot::Scratch: return "Scratch";
        }
        return "Unknown";
    };

    const auto run_hydro = [&](HydroMethod method) -> TracePair {
        ReadyLedger ready(ExecutionSide::Host);
        StageExecutionContext context{ExecutionSide::Host, ready.ledger,
                                      ready.clock};
        const std::vector handles{ready.handle};
        const HydroPlan expected_plan = make_hydro_plan(method);
        std::vector<std::string> physical;
        const auto executor =
            [&](const StageDescriptor& stage, CompletionToken token) {
                physical.push_back(
                    "stage:" + std::to_string(stage.stage)
                    + ":old=" + slot_name(stage.old_slot)
                    + ":input=" + slot_name(stage.input_slot)
                    + ":output=" + slot_name(stage.output_slot));
                return token;
            };
        const auto boundary =
            [&](StateSlot output, StateVersion, CompletionToken token) {
                physical.push_back(
                    std::string("boundary:") + slot_name(output));
                return token;
            };
        const auto rotation =
            [&](arch::state::SlotRotation selected) {
                physical.push_back(
                    std::string("rotate:Current<-")
                    + slot_name(selected.current_from) + ":Next<-"
                    + slot_name(selected.next_from) + ":Scratch<-"
                    + slot_name(selected.scratch_from));
            };
        const auto reflux =
            [&](const HydroPlan& selected, StateSlot slot,
                CompletionToken token) {
                expect(selected.method == method && slot == StateSlot::Current,
                       "Hydro reflux receives exact plan and Current slot");
                const auto before_publication = ready.ledger.inspect(
                    {ready.handle, StateSlot::Current});
                expect(before_publication.interior.version
                           == StateVersion{1 + expected_plan.stages.size()}
                           && before_publication.ghost.residency
                                  == StateResidency::Invalid,
                       "Hydro reflux runs after rotation and before its publication");
                physical.push_back("reflux:Current");
                return token;
            };
        HydroExecutionResult result;
        if (method == HydroMethod::Euler) {
            result = execute_euler_lane(
                context, handles, executor, boundary, rotation, reflux);
        } else if (method == HydroMethod::RK2) {
            result = execute_rk2_lane(
                context, handles, executor, boundary, rotation, reflux);
        } else {
            result = execute_rk3_lane(
                context, handles, executor, boundary, rotation, reflux);
        }
        std::vector<std::string> bookkeeping;
        for (std::size_t index = 0; index < result.stages.size(); ++index) {
            const StageExecutionResult& stage = result.stages[index];
            const StageDescriptor& descriptor = expected_plan.stages[index];
            bookkeeping.push_back(
                "stage:" + std::to_string(descriptor.stage)
                + ":output=" + slot_name(descriptor.output_slot)
                + ":version=" + std::to_string(stage.version.value)
                + ":token="
                + std::to_string(stage.interior_completion.value)
                + ":ghost-token="
                + std::to_string(stage.ghost_completion.value));
        }
        bookkeeping.push_back(
            "reflux:version="
            + std::to_string(result.final_reflux.version.value)
            + ":token="
            + std::to_string(result.final_reflux.completion.value));
        const auto current = ready.ledger.inspect(
            {ready.handle, StateSlot::Current});
        expect(current.interior.version == result.final_reflux.version
                   && current.ghost.residency == StateResidency::Invalid,
               "scheduler publishes Hydro reflux after physical callback");
        bookkeeping.push_back(
            "Current:interior="
            + std::to_string(current.interior.version.value)
            + ":ghost=Invalid:status=success");
        return {std::move(physical), std::move(bookkeeping)};
    };

    const TracePair euler = run_hydro(HydroMethod::Euler);
    const TracePair rk2 = run_hydro(HydroMethod::RK2);
    const TracePair rk3 = run_hydro(HydroMethod::RK3);
    expect(euler.first == std::vector<std::string>({
               "stage:1:old=Current:input=Current:output=Next",
               "rotate:Current<-Next:Next<-Current:Scratch<-Scratch",
               "reflux:Current"}),
           "Euler physical trace is frozen");
    expect(euler.second == std::vector<std::string>({
               "stage:1:output=Next:version=2:token=3:ghost-token=0",
               "reflux:version=3:token=4",
               "Current:interior=3:ghost=Invalid:status=success"}),
           "Euler bookkeeping trace is frozen");
    expect(rk2.first == std::vector<std::string>(
                            {"stage:1:old=Current:input=Current:output=Scratch",
                             "boundary:Scratch",
                             "stage:2:old=Current:input=Scratch:output=Next",
                             "rotate:Current<-Next:Next<-Current:Scratch<-Scratch",
                             "reflux:Current"}),
           "RK2 physical trace is frozen");
    expect(rk2.second == std::vector<std::string>({
               "stage:1:output=Scratch:version=2:token=3:ghost-token=4",
               "stage:2:output=Next:version=3:token=5:ghost-token=0",
               "reflux:version=4:token=6",
               "Current:interior=4:ghost=Invalid:status=success"}),
           "RK2 bookkeeping trace is frozen");
    expect(rk3.first == std::vector<std::string>(
                            {"stage:1:old=Current:input=Current:output=Scratch",
                             "boundary:Scratch",
                             "stage:2:old=Current:input=Scratch:output=Next",
                             "boundary:Next",
                             "stage:3:old=Current:input=Next:output=Scratch",
                             "rotate:Current<-Scratch:Next<-Next:Scratch<-Current",
                             "reflux:Current"}),
           "RK3 physical trace is frozen");
    expect(rk3.second == std::vector<std::string>({
               "stage:1:output=Scratch:version=2:token=3:ghost-token=4",
               "stage:2:output=Next:version=3:token=5:ghost-token=6",
               "stage:3:output=Scratch:version=4:token=7:ghost-token=0",
               "reflux:version=5:token=8",
               "Current:interior=5:ghost=Invalid:status=success"}),
           "RK3 bookkeeping trace is frozen");

    const auto run_rkl = [&](RklMethod method, bool multi,
                            const char* lane) -> TracePair {
        ReadyLedger ready(ExecutionSide::Host);
        StageExecutionContext context{ExecutionSide::Host, ready.ledger,
                                      ready.clock};
        const std::vector handles{ready.handle};
        (void)copy_slot(context, handles, StateSlot::Current,
                        StateSlot::Scratch, [] {});
        (void)copy_slot(context, handles, StateSlot::Current,
                        StateSlot::Next, [] {});
        const RklPlan expected_plan = make_rkl_plan(method, 2);
        std::vector<std::string> physical;
        const auto executor =
            [&](const RklPlan& selected,
                const RklStageDescriptor& stage, CompletionToken token) {
                expect(selected.method == method,
                       "RKL executor receives its production lane method");
                physical.push_back(
                    std::string(lane) + ":stage:"
                    + std::to_string(stage.stage)
                    + ":state-n=" + slot_name(stage.state_n_slot)
                    + ":previous=" + slot_name(stage.previous_slot)
                    + ":older=" + slot_name(stage.older_slot)
                    + ":output=" + slot_name(stage.output_slot));
                return token;
            };
        const auto reflux =
            [&](const RklPlan& selected,
                const RklStageDescriptor& stage, CompletionToken token) {
                expect(selected.method == method,
                       "RKL reflux receives its production lane method");
                const auto before_publication = ready.ledger.inspect(
                    {ready.handle, stage.output_slot});
                expect(before_publication.interior.version == StateVersion{1},
                       "RKL reflux runs before the stage publication");
                physical.push_back(
                    std::string(lane) + ":reflux:"
                    + std::to_string(stage.stage)
                    + ":output=" + slot_name(stage.output_slot));
                return token;
            };
        const auto boundary =
            [&](StateSlot output, StateVersion, CompletionToken token) {
                physical.push_back(
                    std::string(lane) + ":boundary:"
                    + slot_name(output));
                return token;
            };
        const auto rotation = [&](arch::state::SlotRotation selected) {
            physical.push_back(
                std::string(lane) + ":rotate:Current<-"
                + slot_name(selected.current_from) + ":Next<-"
                + slot_name(selected.next_from) + ":Scratch<-"
                + slot_name(selected.scratch_from));
        };
        RklExecutionResult result;
        if (!multi && method == RklMethod::RKL1) {
            result = execute_single_rkl1_lane(
                context, handles, 2, executor, reflux, boundary, rotation);
        } else if (!multi && method == RklMethod::RKL2) {
            result = execute_single_rkl2_lane(
                context, handles, 2, executor, reflux, boundary, rotation);
        } else if (method == RklMethod::RKL1) {
            result = execute_multi_rkl1_lane(
                context, handles, 2, executor, reflux, boundary, rotation);
        } else {
            result = execute_multi_rkl2_lane(
                context, handles, 2, executor, reflux, boundary, rotation);
        }
        expect(result.stages.size() == 2,
               "each RKL production seam executes two requested stages");
        std::vector<std::string> bookkeeping;
        for (std::size_t index = 0; index < result.stages.size(); ++index) {
            const StageExecutionResult& stage = result.stages[index];
            const RklStageDescriptor& descriptor =
                expected_plan.stages[index];
            bookkeeping.push_back(
                std::string(lane) + ":stage:"
                + std::to_string(descriptor.stage)
                + ":output=" + slot_name(descriptor.output_slot)
                + ":version=" + std::to_string(stage.version.value)
                + ":token="
                + std::to_string(stage.interior_completion.value)
                + ":ghost-token="
                + std::to_string(stage.ghost_completion.value));
        }
        const auto current = ready.ledger.inspect(
            {ready.handle, StateSlot::Current});
        expect(current.interior.version == result.stages.back().version
                   && current.ghost_source_version
                          == current.interior.version,
               "RKL runtime lane ends readable after full rotation");
        const auto next = ready.ledger.inspect(
            {ready.handle, StateSlot::Next});
        const auto scratch = ready.ledger.inspect(
            {ready.handle, StateSlot::Scratch});
        bookkeeping.push_back(
            std::string(lane) + ":final:Current="
            + std::to_string(current.interior.version.value)
            + ":Next=" + std::to_string(next.interior.version.value)
            + ":Scratch=" + std::to_string(scratch.interior.version.value)
            + ":ghosts=matched:status=success");
        return {std::move(physical), std::move(bookkeeping)};
    };

    const TracePair single_rkl1 =
        run_rkl(RklMethod::RKL1, false, "single-rkl1");
    const TracePair single_rkl2 =
        run_rkl(RklMethod::RKL2, false, "single-rkl2");
    const TracePair multi_rkl1 =
        run_rkl(RklMethod::RKL1, true, "multi-rkl1");
    const TracePair multi_rkl2 =
        run_rkl(RklMethod::RKL2, true, "multi-rkl2");
    for (const auto& [trace, lane] : {
             std::pair{&single_rkl1, "single-rkl1"},
             std::pair{&single_rkl2, "single-rkl2"},
             std::pair{&multi_rkl1, "multi-rkl1"},
             std::pair{&multi_rkl2, "multi-rkl2"}}) {
        expect(trace->first == std::vector<std::string>{
                   std::string(lane)
                       + ":stage:1:state-n=Current:previous=Current:older=Current:output=Scratch",
                   std::string(lane) + ":reflux:1:output=Scratch",
                   std::string(lane) + ":boundary:Scratch",
                   std::string(lane)
                       + ":stage:2:state-n=Current:previous=Scratch:older=Next:output=Next",
                   std::string(lane) + ":reflux:2:output=Next",
                   std::string(lane) + ":boundary:Next",
                   std::string(lane)
                       + ":rotate:Current<-Next:Next<-Current:Scratch<-Scratch"}
                   && trace->second == std::vector<std::string>{
                       std::string(lane)
                           + ":stage:1:output=Scratch:version=2:token=7:ghost-token=8",
                       std::string(lane)
                           + ":stage:2:output=Next:version=3:token=9:ghost-token=10",
                       std::string(lane)
                           + ":final:Current=3:Next=1:Scratch=2:ghosts=matched:status=success"},
               "each RKL seam freezes separate physical/bookkeeping traces");
    }

    ReadyLedger burn_ready(ExecutionSide::Host);
    StageExecutionContext burn_context{ExecutionSide::Host,
                                       burn_ready.ledger,
                                       burn_ready.clock};
    const std::vector burn_handles{burn_ready.handle};
    std::vector<std::string> burn_physical;
    std::vector<std::string> burn_bookkeeping;
    const PublicationWitness first_burn = execute_burn_first_lane(
        burn_context, burn_handles, [&](CompletionToken token) {
            burn_physical.push_back("burn-first");
            return token;
        });
    const auto first_burn_state = burn_ready.ledger.inspect(
        {burn_ready.handle, StateSlot::Current});
    expect(first_burn_state.interior.version == first_burn.version
               && first_burn_state.ghost.residency
                      == StateResidency::Invalid,
           "first burn publishes interior and invalidates ghost");
    burn_bookkeeping.push_back(
        "burn-first:version=" + std::to_string(first_burn.version.value)
        + ":token=" + std::to_string(first_burn.completion.value)
        + ":ghost=Invalid:status=success");
    const PublicationWitness second_burn = execute_burn_second_lane(
        burn_context, burn_handles, [&](CompletionToken token) {
            burn_physical.push_back("burn-second");
            return token;
        });
    const auto second_burn_state = burn_ready.ledger.inspect(
        {burn_ready.handle, StateSlot::Current});
    expect(second_burn_state.interior.version == second_burn.version
               && second_burn_state.ghost.residency
                      == StateResidency::Invalid,
           "second burn publishes interior and invalidates ghost");
    burn_bookkeeping.push_back(
        "burn-second:version=" + std::to_string(second_burn.version.value)
        + ":token=" + std::to_string(second_burn.completion.value)
        + ":ghost=Invalid:status=success");
    expect(burn_physical
               == std::vector<std::string>({"burn-first", "burn-second"})
               && burn_bookkeeping
                      == std::vector<std::string>({
                          "burn-first:version=2:token=3:ghost=Invalid:status=success",
                          "burn-second:version=3:token=4:ghost=Invalid:status=success"}),
           "Driver burn seams have separate runtime physical/bookkeeping traces");

    const auto emit = [](const char* kind, const char* lane,
                         const std::vector<std::string>& trace) {
        std::cout << "LANE_TRACE\t" << kind << '\t' << lane;
        for (const std::string& event : trace) std::cout << '\t' << event;
        std::cout << '\n';
    };
    emit("physical", "euler", euler.first);
    emit("bookkeeping", "euler", euler.second);
    emit("physical", "rk2", rk2.first);
    emit("bookkeeping", "rk2", rk2.second);
    emit("physical", "rk3", rk3.first);
    emit("bookkeeping", "rk3", rk3.second);
    emit("physical", "single-rkl1", single_rkl1.first);
    emit("bookkeeping", "single-rkl1", single_rkl1.second);
    emit("physical", "single-rkl2", single_rkl2.first);
    emit("bookkeeping", "single-rkl2", single_rkl2.second);
    emit("physical", "multi-rkl1", multi_rkl1.first);
    emit("bookkeeping", "multi-rkl1", multi_rkl1.second);
    emit("physical", "multi-rkl2", multi_rkl2.first);
    emit("bookkeeping", "multi-rkl2", multi_rkl2.second);
    emit("physical", "burn-first", {burn_physical.front()});
    emit("bookkeeping", "burn-first", {burn_bookkeeping.front()});
    emit("physical", "burn-second", {burn_physical.back()});
    emit("bookkeeping", "burn-second", {burn_bookkeeping.back()});
}

void test_boundary_failure_blocks_next_stage_and_rotation()
{
    using namespace arch::scheduler;
    ReadyLedger ready(ExecutionSide::Host);
    StageExecutionContext context{ExecutionSide::Host, ready.ledger,
                                  ready.clock};
    const std::vector handles{ready.handle};
    const HydroPlan plan = make_hydro_plan(HydroMethod::RK2);
    expect_throws<std::runtime_error>(
        [&] {
            (void)execute_stage(
                context, handles, plan.stages[0],
                [](const StageDescriptor&, CompletionToken token) {
                    return token;
                },
                [](StateSlot, StateVersion,
                   CompletionToken) -> CompletionToken {
                    throw std::runtime_error("boundary failure");
                });
        },
        "boundary failure propagates");
    const auto scratch =
        ready.ledger.inspect({ready.handle, StateSlot::Scratch});
    expect(scratch.interior.residency == StateResidency::HostValid
               && scratch.ghost.residency == StateResidency::Invalid,
           "boundary failure keeps interior and invalid ghost");

    bool stage2_called = false;
    expect_throws<std::logic_error>(
        [&] {
            (void)execute_stage(
                context, handles, plan.stages[1],
                [&](const StageDescriptor&, CompletionToken token) {
                    stage2_called = true;
                    return token;
                },
                [](StateSlot, StateVersion, CompletionToken token) {
                    return token;
                });
        },
        "next stage rejects missing matching ghost");
    expect(!stage2_called, "mismatched ghost blocks executor");

    bool physical_rotation = false;
    const std::vector bad_handles{
        ready.handle, amr::BlockHandle{{99}, {1}}};
    expect_throws<std::invalid_argument>(
        [&] {
            rotate_slots(context, bad_handles, plan.final_rotation,
                         [&] { physical_rotation = true; });
        },
        "failed prerequisite prevents rotation path");
    expect(!physical_rotation, "failed path does not rotate physical state");
}

void test_real_registration_batches_across_topology_epoch()
{
    using namespace arch::scheduler;
    TopologyIdentityRegistry registry({1, {3, 1, 1}, 2});
    const auto a = key(1, 0, 0);
    const auto b = key(1, 0, 1);
    const auto c = key(1, 0, 2);
    const auto adopted = adopt_topology(
        registry, std::vector{observation(1, a), observation(2, b)});
    MonotonicSchedulerClock clock;
    StateResidencyLedger initial(adopted.epoch);
    const PublicationWitness initial_witness = clock.next_publication();
    for (const amr::BlockHandle handle
         : adopted.handles_in_observation_order) {
        initial.register_block(handle, initial_witness.version,
                               initial_witness.completion);
        const auto current = initial.inspect({handle, StateSlot::Current});
        expect(current.interior.version == initial_witness.version
                   && current.interior.completion
                          == initial_witness.completion,
               "initial registration fans out one version/token");
        expect(initial.inspect({handle, StateSlot::Next})
                       .interior.residency == StateResidency::Invalid
                   && initial.inspect({handle, StateSlot::Scratch})
                          .interior.residency == StateResidency::Invalid,
               "register_block alone creates invalid work slots");
    }

    const amr::BlockHandle stale_a = registry.handle_for(a);
    const auto changed = reconcile_topology(
        registry, std::vector{observation(9, a), observation(3, c)});
    expect(changed.topology_changed, "changed topology uses fresh epoch");
    StateResidencyLedger replacement(changed.epoch);
    const PublicationWitness topology_witness = clock.next_publication();
    for (const amr::BlockHandle handle
         : changed.handles_in_observation_order) {
        replacement.register_block(handle, topology_witness.version,
                                   topology_witness.completion);
        const auto current =
            replacement.inspect({handle, StateSlot::Current});
        expect(current.interior.version == topology_witness.version
                   && current.interior.completion
                          == topology_witness.completion,
               "survivor/new registration shares topology witness");
    }
    expect(registry.handle_for(a).uid == stale_a.uid
               && registry.handle_for(a).epoch == changed.epoch,
           "survivor retains UID and binds new epoch");
    expect_throws<std::invalid_argument>(
        [&] { (void)replacement.inspect({stale_a, StateSlot::Current}); },
        "fresh ledger rejects old epoch handle");

    StageExecutionContext context{ExecutionSide::Host, replacement, clock};
    (void)complete_boundary(
        context, changed.handles_in_observation_order,
        StateSlot::Current, topology_witness.version,
        [](StateSlot, StateVersion, CompletionToken token) {
            return token;
        });
    for (const amr::BlockHandle handle
         : changed.handles_in_observation_order) {
        const auto current =
            replacement.inspect({handle, StateSlot::Current});
        expect(current.ghost_source_version == topology_witness.version,
               "topology ghost publishes from exact topology version");
    }
    const amr::BlockHandle first_changed =
        changed.handles_in_observation_order.front();
    const auto before_replay =
        replacement.inspect({first_changed, StateSlot::Current});
    expect_throws<std::logic_error>(
        [&] {
            replacement.publish_interior(
                {first_changed, StateSlot::Current}, ExecutionSide::Host,
                {topology_witness.version.value + 1},
                topology_witness.completion);
        },
        "old topology token rejected on same region");
    expect_slot_equal(
        replacement.inspect({first_changed, StateSlot::Current}),
        before_replay, "rejected topology replay mutates nothing");
    const PublicationWitness next = publish_completed_interior(
        context, changed.handles_in_observation_order, StateSlot::Current);
    expect(next.version.value > topology_witness.version.value
               && next.completion.value
                      > topology_witness.completion.value,
           "strictly larger post-topology witness accepted");
    expect(clock.last_version() == topology_witness.version.value
                   + 1
               && clock.last_token() == next.completion.value,
           "persistent clock survives topology replacement and boundary");
}

void test_persistent_clock_and_overflow()
{
    using arch::scheduler::MonotonicSchedulerClock;
    MonotonicSchedulerClock clock;
    const auto first = clock.next_publication();
    const auto boundary = clock.next_completion();
    const auto second = clock.next_publication();
    expect(first.version == StateVersion{1}
               && first.completion == CompletionToken{1, CompletionState::Complete},
           "first publication witness");
    expect(boundary == CompletionToken{2, CompletionState::Complete},
           "completion token advances without version");
    expect(second.version == StateVersion{2}
               && second.completion.value == 3,
           "publication clocks stay monotonic");

    MonotonicSchedulerClock token_overflow(
        std::numeric_limits<std::uint64_t>::max(), 1);
    expect_throws<std::overflow_error>(
        [&] { (void)token_overflow.next_completion(); },
        "token overflow fatal before wrap");
    MonotonicSchedulerClock version_overflow(
        1, std::numeric_limits<std::uint64_t>::max());
    expect_throws<std::overflow_error>(
        [&] { (void)version_overflow.next_publication(); },
        "version overflow fatal before publication");
}

void test_scoped_production_context_binding()
{
    using namespace arch::scheduler;
    ReadyLedger ready(ExecutionSide::Host);
    StageExecutionContext context{ExecutionSide::Host, ready.ledger,
                                  ready.clock};
    const std::vector handles{ready.handle};
    expect_throws<std::logic_error>(
        [] { (void)current_stage_binding(); },
        "unbound production lane rejected");
    {
        ScopedStageBinding scope(context, handles);
        const StageBinding& binding = current_stage_binding();
        expect(&binding.context == &context, "scope exposes exact context");
        expect(binding.handles.size() == 1
                   && binding.handles.front() == ready.handle,
               "scope exposes handles without pool ids");
        expect_throws<std::logic_error>(
            [&] { ScopedStageBinding nested(context, handles); },
            "nested production binding rejected");
    }
    expect_throws<std::logic_error>(
        [] { (void)current_stage_binding(); },
        "scope restores unbound state");
}

std::string read_source(const char* relative)
{
    std::ifstream input(std::string(ARCH_SOURCE_DIR) + "/" + relative);
    if (!input) {
        std::cerr << "FAIL: cannot read production source: " << relative << '\n';
        std::exit(EXIT_FAILURE);
    }
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::size_t source_occurrences(const std::string& source,
                               const std::string& needle)
{
    std::size_t count = 0;
    for (std::size_t offset = 0;
         (offset = source.find(needle, offset)) != std::string::npos;
         offset += needle.size()) {
        ++count;
    }
    return count;
}

void test_driver_production_ledger_wiring()
{
    const std::string source = read_source("src/driver/Driver.h");
    expect(source.find("TopologyIdentityRegistry topology_registry")
               != std::string::npos,
           "Driver owns stable logical identity reconciliation");
    expect(source.find("register_block(handle, initial_witness.version")
               != std::string::npos,
           "Driver initial adoption uses real register_block");
    expect(source.find("topology_registry.stage_reconciliation(")
               != std::string::npos
               && source.find("topology_registry.commit_after_success(")
                      != std::string::npos,
           "Driver stages topology and commits only after finalization");
    expect(source.find("ScopedStageBinding stage_binding")
               != std::string::npos,
           "Driver binds one shared scheduler to production lanes");
    expect(source_occurrences(source, "execute_burn_first_lane(") == 1
               && source_occurrences(source,
                                     "execute_burn_second_lane(") == 1,
           "Driver names both burn production seams exactly once");
}

void test_euler_production_lane_uses_shared_plan()
{
    const std::string source = read_source(
        "src/numerics/integrator/TimeIntegratorEuler.h");
    expect(source.find("execute_euler_lane(") != std::string::npos,
           "Euler production consumes its runtime-tested seam");
    expect(source.find("descriptor.old_slot") != std::string::npos
               && source.find("descriptor.input_slot") != std::string::npos
               && source.find("descriptor.output_slot") != std::string::npos,
           "Euler physical state selection follows descriptor slots");
    expect(source.find("execute_stage(") == std::string::npos
               && source.find("rotate_slots(") == std::string::npos,
           "Euler adapter retains no private scheduler loop");
}

void test_rk2_production_lane_uses_shared_plan()
{
    const std::string source = read_source(
        "src/numerics/integrator/TimeIntegratorRK2.h");
    expect(source.find("execute_rk2_lane(") != std::string::npos,
           "RK2 production consumes its runtime-tested seam");
    expect(source.find("execute_stage(") == std::string::npos
               && source.find("rotate_slots(") == std::string::npos,
           "RK2 adapter retains no private scheduler loop");
}

void test_rk3_production_lane_uses_shared_plan()
{
    const std::string source = read_source(
        "src/numerics/integrator/TimeIntegratorRK3.h");
    expect(source.find("execute_rk3_lane(") != std::string::npos,
           "RK3 production consumes its runtime-tested seam");
    expect(source.find("execute_stage(") == std::string::npos
               && source.find("rotate_slots(") == std::string::npos,
           "RK3 adapter retains no private scheduler loop");
}

void test_rkl_production_lanes_use_shared_plan()
{
    const std::string rkl1 = read_source(
        "src/numerics/diffusion/RKL1TimeIntegrator.h");
    const std::string rkl2 = read_source(
        "src/numerics/diffusion/RKL2TimeIntegrator.h");
    const std::string stages = read_source(
        "src/numerics/diffusion/DiffusionAMRStages.h");
    expect(rkl1.find("advance_single_rkl(") != std::string::npos
               && rkl1.find("RklMethod::RKL1") != std::string::npos,
           "single RKL1 routes through shared descriptor executor");
    expect(rkl2.find("advance_single_rkl(") != std::string::npos
               && rkl2.find("RklMethod::RKL2") != std::string::npos,
           "single RKL2 routes through shared descriptor executor");
    expect(rkl1.find("advance_amr_rkl1(") != std::string::npos
               && rkl1.find("advance_amr_rkl(amr_ctrl")
                      != std::string::npos
               && rkl1.find("RklMethod::RKL1") != std::string::npos,
           "multi RKL1 routes through shared descriptor executor");
    expect(rkl2.find("advance_amr_rkl2(") != std::string::npos
               && rkl2.find("advance_amr_rkl(amr_ctrl")
                      != std::string::npos
               && rkl2.find("RklMethod::RKL2") != std::string::npos,
           "multi RKL2 routes through shared descriptor executor");
    expect(source_occurrences(stages, "execute_single_rkl1_lane(") == 1
               && source_occurrences(stages,
                                     "execute_single_rkl2_lane(") == 1
               && source_occurrences(stages,
                                     "execute_multi_rkl1_lane(") == 1
               && source_occurrences(stages,
                                     "execute_multi_rkl2_lane(") == 1,
           "all four RKL production routes consume runtime-tested seams");
    expect(stages.find("execute_rkl_stage(") == std::string::npos
               && stages.find("rotate_slots(") == std::string::npos,
           "RKL physical adapters retain no private scheduler loop");
}

void test_production_lane_fingerprints_and_authority_absence()
{
    const std::string euler = read_source(
        "src/numerics/integrator/TimeIntegratorEuler.h");
    const std::string rk2 = read_source(
        "src/numerics/integrator/TimeIntegratorRK2.h");
    const std::string rk3 = read_source(
        "src/numerics/integrator/TimeIntegratorRK3.h");
    for (const std::string* lane : {&euler, &rk2, &rk3}) {
        expect(source_occurrences(*lane, "execute_stage(") == 0
                   && source_occurrences(*lane, "rotate_slots(") == 0,
               "each Hydro adapter delegates scheduler ordering");
        expect(source_occurrences(*lane, "amr_ctrl.ApplyReflux(dt)") == 1,
               "each Hydro physical adapter supplies one reflux callback");
        expect(source_occurrences(*lane, "publish_completed_interior(") == 0,
               "Hydro adapters cannot publish around the scheduler seam");
        expect(lane->find("descriptor.old_slot") != std::string::npos
                   && lane->find("descriptor.input_slot")
                          != std::string::npos
                   && lane->find("descriptor.output_slot")
                          != std::string::npos,
               "each Hydro physical route consumes descriptor slots");
    }

    const std::string rkl1 = read_source(
        "src/numerics/diffusion/RKL1TimeIntegrator.h");
    const std::string rkl2 = read_source(
        "src/numerics/diffusion/RKL2TimeIntegrator.h");
    expect(rkl1.find("for (") == std::string::npos
               && rkl2.find("for (") == std::string::npos,
           "RKL policy adapters retain no second stage loop");
    expect(source_occurrences(rkl1, "RklMethod::RKL1") == 2
               && source_occurrences(rkl2, "RklMethod::RKL2") == 2,
           "single and multi RKL wrappers select their exact method");

    const std::string diffusion = read_source(
        "src/numerics/diffusion/DiffusionAMRStages.h");
    expect(source_occurrences(diffusion, "execute_rkl_stage(") == 0
               && source_occurrences(diffusion, "rotate_slots(") == 0,
           "both RKL physical executors delegate publication and rotation");
    expect(source_occurrences(diffusion, "amr_ctrl.ApplyReflux(") == 1,
           "multi RKL has one descriptor-loop reflux point");
    expect(source_occurrences(diffusion, "detail::synchronize(") == 3
               && source_occurrences(diffusion, "complete_boundary(") == 3,
           "RKL keeps initial/per-stage/final boundary completion points");
    expect(diffusion.find("static FluidState Y") == std::string::npos
               && diffusion.find("copy_stage_to_solution")
                      == std::string::npos,
           "legacy static logical buffers and final copy are removed");

    const std::string driver = read_source("src/driver/Driver.h");
    const std::size_t first_burn = driver.find(
        "BlockReductionComponent::BurnFirstHalf");
    const std::size_t second_burn = driver.find(
        "BlockReductionComponent::BurnSecondHalf");
    const std::size_t first_seam = driver.find("execute_burn_first_lane(");
    const std::size_t second_seam = driver.find("execute_burn_second_lane(");
    expect(first_seam < first_burn && first_burn < second_seam
               && second_seam < second_burn,
           "Driver burn physical work remains inside independent seams");
    expect(source_occurrences(driver,
                              "MonotonicSchedulerClock scheduler_clock;")
               == 1,
           "Driver owns one run-persistent scheduler clock");
    expect(source_occurrences(driver, "register_block(") == 3,
           "Driver registers initial Host, regridded Host and staged Device authority");
    expect(source_occurrences(driver, "initial_witness.completion") == 1
               && source_occurrences(driver,
                                     "topology_witness.completion") == 3,
           "each regrid authority fans out its batch token; Device also publishes migrated ghosts");
    const auto migration_complete = driver.find("prepared.CompleteDeviceMigration()");
    const auto device_registration = driver.find("payload.ledger->register_block(");
    const auto device_ghost = driver.find("payload.ledger->publish_ghost(");
    const auto ready = driver.find("transaction.mark_ready()", device_ghost);
    expect(migration_complete < device_registration && device_registration < device_ghost
               && device_ghost < ready && ready != std::string::npos
               && driver.find("payload.topology_witness.completion, ExecutionSide::Device)")
                      != std::string::npos,
           "completed device migration publishes Device interiors/ghosts before transactional commit");
    expect(driver.find("handle.uid.value") == std::string::npos,
           "registration batch witnesses never depend on handle identity");
    expect(driver.find("scheduler_clock =") == std::string::npos,
           "Driver never resets persistent scheduler clock");
    expect(driver.find("residency_ledger->publish_") == std::string::npos,
           "Driver cannot bypass scheduler publication helpers");
    expect(driver.find("current_interior_version()") != std::string::npos,
           "Driver boundary publication uses authoritative Current version");
    expect(source_occurrences(driver, "current_interior_version(),") == 1,
           "Driver's unified boundary hook publishes from exact Current version");
    expect(euler.find("std::vector<StageDescriptor>") == std::string::npos
               && rk2.find("std::vector<StageDescriptor>")
                      == std::string::npos
               && rk3.find("std::vector<StageDescriptor>")
                      == std::string::npos,
           "Hydro adapters contain no second descriptor table");

    const std::string scheduler = read_source("src/driver/StageScheduler.h");
    expect(scheduler.find("begin_transfer") == std::string::npos
               && scheduler.find("PendingH2D") == std::string::npos
               && scheduler.find("PendingD2H") == std::string::npos,
           "ordinary stages never fabricate E0 transfer state");
    expect(scheduler.find("cuda") == std::string::npos
               && scheduler.find("CUDA") == std::string::npos
               && scheduler.find(".cuh") == std::string::npos,
           "shared scheduler has no CUDA header/runtime leakage");
    expect(scheduler.find("pool_index") == std::string::npos
               && scheduler.find("morton") == std::string::npos,
           "shared scheduler exposes no Host lowering identity");
    expect(scheduler.find("DiffFunction") == std::string::npos,
           "shared descriptor duplicates no RKL coefficient formulas");
    expect(scheduler.find("duplicated_rkl") == std::string::npos,
           "shared descriptor contains no copied RKL coefficient authority");

    const std::string topology = read_source(
        "src/driver/TopologyIdentityRegistry.h");
    expect(topology.find("#include <utility>") != std::string::npos,
           "topology registry includes utility for std::move directly");
}

} // namespace

int main()
{
    test_identity_first_reconciliation();
    test_permutation_and_pool_move_preserve_epoch();
    test_domain_bounds_and_global_roots();
    test_snapshot_validation_precedes_lowering();
    test_staged_topology_visibility_and_snapshot_completeness();
    test_candidate_transaction_safety();
    test_hydro_and_rkl_descriptor_fingerprints();
    test_execution_side_and_failure_publication_order();
    test_boundary_completion_and_full_rotation();
    test_full_logical_slot_copy();
    test_fake_cuda_rkl_uses_shared_descriptors();
    test_scheduler_owned_reflux_and_runtime_lane_traces();
    test_boundary_failure_blocks_next_stage_and_rotation();
    test_real_registration_batches_across_topology_epoch();
    test_persistent_clock_and_overflow();
    test_scoped_production_context_binding();
    test_driver_production_ledger_wiring();
    test_euler_production_lane_uses_shared_plan();
    test_rk2_production_lane_uses_shared_plan();
    test_rk3_production_lane_uses_shared_plan();
    test_rkl_production_lanes_use_shared_plan();
    test_production_lane_fingerprints_and_authority_absence();
    return EXIT_SUCCESS;
}
