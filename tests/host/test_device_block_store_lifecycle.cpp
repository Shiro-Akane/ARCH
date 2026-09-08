/**
 * @file test_device_block_store_lifecycle.cpp
 * @brief Verify staged device-store publication and retirement rules.
 *
 * These host-side contract tests exercise candidate ownership, delayed release
 * and rejected layouts; they do not substitute for CUDA allocation tests.
 */
#include "cuda/runtime/DeviceBlockStore.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using arch::backend::BackendStateAccess;
using arch::backend::StorageGeneration;
using arch::cuda::DeviceArenaSlot;
using arch::cuda::DeviceBlockRecord;
using arch::cuda::DeviceBlockStoreLifecycle;
using arch::cuda::DeviceLayoutGeneration;
using arch::cuda::DeviceMigrationAccess;
using arch::cuda::DeviceMigrationRole;
using arch::cuda::DeviceRetirementFence;
using arch::cuda::DeviceStoreEntry;
using arch::cuda::DeviceStoreProposal;
using arch::state::StateSlot;

static_assert(!std::is_copy_constructible_v<DeviceBlockStoreLifecycle>);
static_assert(!std::is_move_constructible_v<DeviceBlockStoreLifecycle>);
static_assert(!std::is_copy_constructible_v<
              DeviceBlockStoreLifecycle::Candidate>);
static_assert(std::is_move_constructible_v<
              DeviceBlockStoreLifecycle::Candidate>);
static_assert(!std::is_move_assignable_v<
              DeviceBlockStoreLifecycle::Candidate>);
static_assert(std::is_same_v<
              decltype(std::declval<
                       const DeviceBlockStoreLifecycle::Candidate&>().scope()),
              amr::AmrPlanScope>);

void require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Exception = std::exception, typename Function>
void require_rejected(Function&& function, std::string_view message)
{
    bool rejected = false;
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        rejected = true;
    }
    require(rejected, message);
}

constexpr amr::BlockHandle handle(
    std::uint64_t uid, std::uint64_t epoch) noexcept
{
    return {{uid}, {epoch}};
}

constexpr BackendStateAccess access(
    amr::BlockHandle block, std::uint64_t storage,
    StateSlot slot = StateSlot::Current) noexcept
{
    return {block, StorageGeneration{storage}, slot};
}

constexpr DeviceStoreEntry entry(
    std::uint64_t uid, std::uint64_t epoch, std::uint64_t storage,
    std::uint64_t layout, std::uint64_t arena) noexcept
{
    return {
        DeviceBlockRecord{
            handle(uid, epoch), StorageGeneration{storage},
            DeviceLayoutGeneration{layout}},
        DeviceArenaSlot{arena}};
}

constexpr DeviceStoreProposal proposal(
    std::uint64_t uid, std::uint64_t epoch,
    std::uint64_t storage) noexcept
{
    return {handle(uid, epoch), StorageGeneration{storage}};
}

void test_caller_issued_identity_and_shared_scope()
{
    const std::vector<DeviceStoreEntry> initial{
        entry(11, 7, 101, 201, 1), entry(12, 7, 102, 202, 2)};
    DeviceBlockStoreLifecycle store(initial);
    require(store.active_epoch() == amr::TopologyEpoch{7}
                && store.active_entries().size() == 2,
            "initial device namespace drifted");

    const amr::AmrPlanScope scope{41, {7}, {8}};
    const std::vector<DeviceStoreProposal> proposed{
        proposal(11, 8, 103), proposal(13, 8, 104)};
    auto candidate = store.prepare(scope, proposed);
    const auto staged = candidate.entries();
    require(candidate.scope() == scope,
            "device candidate did not retain the shared AMR scope");
    require(staged.size() == 2
                && staged[0].record.storage == StorageGeneration{103}
                && staged[1].record.storage == StorageGeneration{104},
            "device store replaced caller-issued storage generations");
    require(staged[0].record.layout == DeviceLayoutGeneration{203}
                && staged[1].record.layout == DeviceLayoutGeneration{204}
                && staged[0].arena == DeviceArenaSlot{3}
                && staged[1].arena == DeviceArenaSlot{4},
            "staged layout/arena allocation drifted");

    require(!store.contains(
                {staged[0].record.handle, staged[0].record.storage,
                 StateSlot::Current}),
            "ordinary lookup exposed an unpublished device identity");
    require(store.contains_migration(
                candidate,
                {scope, access(handle(11, 7), 101),
                 DeviceMigrationRole::ActiveOldSource})
                && store.contains_migration(
                    candidate,
                    {scope, access(handle(11, 8), 103),
                     DeviceMigrationRole::StagedNewDestination}),
            "shared-scope migration could not address both namespaces");

    auto wrong_scope = scope;
    wrong_scope.transaction_id = 42;
    require_rejected(
        [&] {
            (void)store.migration_record(
                candidate,
                {wrong_scope, access(handle(11, 7), 101),
                 DeviceMigrationRole::ActiveOldSource});
        },
        "a different AMR transaction reached migration storage");
    wrong_scope = scope;
    wrong_scope.to_epoch = amr::TopologyEpoch{9};
    require_rejected(
        [&] {
            (void)store.migration_record(
                candidate,
                {wrong_scope, access(handle(11, 8), 103),
                 DeviceMigrationRole::StagedNewDestination});
        },
        "a different AMR epoch reached staged storage");

    store.abort(std::move(candidate));
    require(store.active_epoch() == amr::TopologyEpoch{7}
                && store.contains(access(handle(11, 7), 101))
                && !store.has_staged_transaction(),
            "abort changed the active device namespace");
    require_rejected(
        [&] { (void)candidate.entries(); },
        "an aborted device candidate remained usable");

    // The shared issuer has already consumed 103/104 even though staging was
    // aborted.  The store therefore accepts only fresh caller identities.
    require_rejected(
        [&] {
            auto stale = store.prepare(
                amr::AmrPlanScope{42, {7}, {8}},
                std::vector<DeviceStoreProposal>{proposal(11, 8, 104)});
            (void)stale;
        },
        "an aborted caller storage generation was reused");
    require(!store.has_staged_transaction(),
            "rejected storage reuse leaked a staged transaction");
}

void test_publish_visibility_and_delayed_retirement()
{
    const std::vector<DeviceStoreEntry> initial{
        entry(21, 9, 301, 401, 1), entry(22, 9, 302, 402, 2)};
    DeviceBlockStoreLifecycle store(initial);
    const amr::AmrPlanScope first_scope{51, {9}, {10}};
    auto first = store.prepare(
        first_scope,
        std::vector<DeviceStoreProposal>{
            proposal(21, 10, 303), proposal(23, 10, 304)});
    const std::vector<DeviceStoreEntry> first_entries(
        first.entries().begin(), first.entries().end());

    int publish_calls = 0;
    const DeviceRetirementFence fence{71};
    store.publish_after_success(
        std::move(first), fence,
        [&](std::span<const DeviceStoreEntry> staged) {
            ++publish_calls;
            require(std::equal(staged.begin(), staged.end(),
                               first_entries.begin(), first_entries.end()),
                    "publication finalizer saw a different namespace");
        });
    require(publish_calls == 1
                && store.active_epoch() == amr::TopologyEpoch{10}
                && store.contains(access(handle(21, 10), 303))
                && !store.contains(access(handle(21, 9), 301))
                && store.has_retirement(fence),
            "publication did not atomically replace active visibility");

    // Arenas 1/2 are retired and 3/4 are active, so none may be reused yet.
    auto pending = store.prepare(
        amr::AmrPlanScope{52, {10}, {11}},
        std::vector<DeviceStoreProposal>{
            proposal(21, 11, 305), proposal(24, 11, 306)});
    require(pending.entries()[0].arena == DeviceArenaSlot{5}
                && pending.entries()[1].arena == DeviceArenaSlot{6},
            "an arena was reused before its retirement fence");
    store.abort(std::move(pending));

    require_rejected(
        [&] {
            store.complete_retirement(
                DeviceRetirementFence{72}, [](auto) noexcept {});
        },
        "an unknown retirement fence was accepted");
    std::size_t retired_count = 0;
    store.complete_retirement(
        fence, [&](std::span<const DeviceStoreEntry> retired) noexcept {
            retired_count = retired.size();
        });
    require(retired_count == 2 && !store.has_retirement(fence),
            "the exact fence did not release its retired namespace");

    auto reuse = store.prepare(
        amr::AmrPlanScope{53, {10}, {11}},
        std::vector<DeviceStoreProposal>{
            proposal(21, 11, 307), proposal(24, 11, 308)});
    require(reuse.entries()[0].arena == DeviceArenaSlot{1}
                && reuse.entries()[1].arena == DeviceArenaSlot{2},
            "fence-complete arenas were not reusable");
    store.abort(std::move(reuse));
}

void test_failed_publication_and_candidate_raii()
{
    const std::vector<DeviceStoreEntry> initial{
        entry(31, 12, 501, 601, 1)};
    DeviceBlockStoreLifecycle store(initial);
    auto candidate = store.prepare(
        amr::AmrPlanScope{61, {12}, {13}},
        std::vector<DeviceStoreProposal>{proposal(31, 13, 502)});

    int finalizer_calls = 0;
    require_rejected<std::runtime_error>(
        [&] {
            store.publish_after_success(
                std::move(candidate), DeviceRetirementFence{81},
                [&](auto) {
                    ++finalizer_calls;
                    throw std::runtime_error("injected publication failure");
                });
        },
        "a throwing publication finalizer was accepted");
    require(finalizer_calls == 1
                && store.active_epoch() == amr::TopologyEpoch{12}
                && store.contains(access(handle(31, 12), 501))
                && !store.contains(access(handle(31, 13), 502))
                && !store.has_retirement(DeviceRetirementFence{81}),
            "failed publication changed active or retirement state");
    require_rejected(
        [&] {
            store.publish_after_success(
                std::move(candidate), DeviceRetirementFence{82},
                [](auto) {});
        },
        "a failed publication attempt was retried");
    store.abort(std::move(candidate));
    require(!store.has_staged_transaction(),
            "explicit rollback left a staged transaction");

    {
        auto abandoned = store.prepare(
            amr::AmrPlanScope{62, {12}, {13}},
            std::vector<DeviceStoreProposal>{proposal(31, 13, 503)});
        require(store.has_staged_transaction()
                    && abandoned.entries().size() == 1,
                "RAII rollback fixture was not staged");
    }
    require(!store.has_staged_transaction()
                && store.contains(access(handle(31, 12), 501)),
            "candidate destruction changed active state or leaked staging");

    auto fresh = store.prepare(
        amr::AmrPlanScope{63, {12}, {13}},
        std::vector<DeviceStoreProposal>{proposal(31, 13, 504)});
    require(fresh.entries()[0].record.storage == StorageGeneration{504}
                && fresh.entries()[0].record.layout
                    == DeviceLayoutGeneration{604},
            "rollback reused a consumed identity generation");
    store.abort(std::move(fresh));
}

void test_validation_and_overflow_guards()
{
    require_rejected(
        [] {
            const std::vector<DeviceStoreEntry> duplicate_arena{
                entry(41, 20, 701, 801, 1),
                entry(42, 20, 702, 802, 1)};
            DeviceBlockStoreLifecycle invalid(duplicate_arena);
        },
        "duplicate active arenas were accepted");

    const std::vector<DeviceStoreEntry> initial{
        entry(41, 20, 701, 801, 1), entry(42, 20, 702, 802, 2)};
    DeviceBlockStoreLifecycle store(initial);
    require_rejected(
        [&] {
            auto wrong_epoch = store.prepare(
                amr::AmrPlanScope{91, {19}, {21}},
                std::vector<DeviceStoreProposal>{proposal(41, 21, 703)});
            (void)wrong_epoch;
        },
        "a stale from-epoch was accepted");
    require_rejected(
        [&] {
            auto duplicate_handle = store.prepare(
                amr::AmrPlanScope{91, {20}, {21}},
                std::vector<DeviceStoreProposal>{
                    proposal(41, 21, 703), proposal(41, 21, 704)});
            (void)duplicate_handle;
        },
        "duplicate proposed handles were accepted");
    require_rejected(
        [&] {
            auto duplicate_storage = store.prepare(
                amr::AmrPlanScope{91, {20}, {21}},
                std::vector<DeviceStoreProposal>{
                    proposal(41, 21, 703), proposal(43, 21, 703)});
            (void)duplicate_storage;
        },
        "duplicate caller storage generations were accepted");
    require(!store.has_staged_transaction()
                && store.contains(access(handle(41, 20), 701)),
            "proposal validation changed committed state");

    const std::vector<DeviceStoreEntry> exhausted_layout{
        entry(51, 30, 901, std::numeric_limits<std::uint64_t>::max(), 1)};
    DeviceBlockStoreLifecycle layout_store(exhausted_layout);
    require_rejected<std::overflow_error>(
        [&] {
            auto impossible = layout_store.prepare(
                amr::AmrPlanScope{101, {30}, {31}},
                std::vector<DeviceStoreProposal>{proposal(51, 31, 902)});
            (void)impossible;
        },
        "layout-generation exhaustion was not reported");
    require(!layout_store.has_staged_transaction()
                && layout_store.contains(access(handle(51, 30), 901)),
            "layout overflow changed committed state");

    DeviceBlockStoreLifecycle fence_store(
        std::vector<DeviceStoreEntry>{entry(61, 40, 1001, 1101, 1)});
    auto maximum_fence = fence_store.prepare(
        amr::AmrPlanScope{111, {40}, {41}},
        std::vector<DeviceStoreProposal>{proposal(61, 41, 1002)});
    fence_store.publish_after_success(
        std::move(maximum_fence),
        DeviceRetirementFence{std::numeric_limits<std::uint64_t>::max()},
        [](auto) {});
    fence_store.complete_retirement(
        DeviceRetirementFence{std::numeric_limits<std::uint64_t>::max()},
        [](auto) noexcept {});

    auto exhausted_fence = fence_store.prepare(
        amr::AmrPlanScope{112, {41}, {42}},
        std::vector<DeviceStoreProposal>{proposal(61, 42, 1003)});
    require_rejected<std::overflow_error>(
        [&] {
            fence_store.publish_after_success(
                std::move(exhausted_fence), DeviceRetirementFence{1},
                [](auto) {});
        },
        "retirement-fence exhaustion was not reported");
    require(fence_store.active_epoch() == amr::TopologyEpoch{41}
                && fence_store.contains(access(handle(61, 41), 1002)),
            "fence overflow changed the active namespace");
    fence_store.abort(std::move(exhausted_fence));
}

} // namespace

int main()
{
    try {
        test_caller_issued_identity_and_shared_scope();
        test_publish_visibility_and_delayed_retirement();
        test_failed_publication_and_candidate_raii();
        test_validation_and_overflow_guards();
        std::cout << "device block store lifecycle contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
