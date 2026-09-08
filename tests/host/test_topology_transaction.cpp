/**
 * @file test_topology_transaction.cpp
 * @brief Check atomic publication and rollback of AMR topology changes.
 *
 * Exercise prepare/commit/abort ordering, allocation failures and restoration
 * of block identities, field ownership and residency metadata.
 */
#include "amr/TopologyTransaction.h"
#include "amr/AmrTree.h"
#include "driver/StageScheduler.h"
#include "driver/TopologyIdentityRegistry.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

static_assert(!std::is_copy_constructible_v<amr::TopologyTransaction>);
static_assert(!std::is_copy_assignable_v<amr::TopologyTransaction>);
static_assert(std::is_move_constructible_v<amr::TopologyTransaction>);
static_assert(!std::is_move_assignable_v<amr::TopologyTransaction>);

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_rejected(Function&& function, const std::string& message)
{
    bool rejected = false;
    try {
        function();
    } catch (const std::logic_error&) {
        rejected = true;
    }
    expect(rejected, message);
}

struct PreparedPayload {
    int value = 0;
};

class InjectedStagedAllocationFailure final : public std::runtime_error {
public:
    InjectedStagedAllocationFailure()
        : std::runtime_error("injected staged AMR allocation failure")
    {
    }
};

void test_success_order()
{
    amr::TopologyTransaction transaction(41, {7}, {8});
    expect(transaction.state() == amr::TopologyTransactionState::Prepared,
           "transaction did not begin prepared");
    transaction.begin_migration();
    transaction.mark_ready();

    std::vector<std::string> order;
    order.reserve(3);
    int published = 0;
    transaction.commit_after_success(
        [&](const amr::AmrPlanScope& scope) {
            expect(scope.transaction_id == 41
                       && scope.from_epoch.value == 7
                       && scope.to_epoch.value == 8,
                   "transaction scope drifted");
            order.push_back("prepare");
            return PreparedPayload{17};
        },
        [&](const amr::AmrPlanScope&, PreparedPayload& payload) {
            expect(payload.value == 17, "prepared payload drifted");
            order.push_back("finalize");
        },
        [&](PreparedPayload&& payload) noexcept {
            published = payload.value;
            order.push_back("publish");
        });

    expect(order == std::vector<std::string>{"prepare", "finalize", "publish"},
           "transaction callback order drifted");
    expect(published == 17, "transaction publish payload drifted");
    expect(transaction.state() == amr::TopologyTransactionState::Committed,
           "transaction did not commit");
    expect_rejected([&] { transaction.begin_migration(); },
                    "committed transaction restarted");
    expect_rejected([&] { transaction.abort([]() noexcept {}); },
                    "committed transaction aborted");
}

void test_prepare_failure_requires_abort()
{
    amr::TopologyTransaction transaction(42, {8}, {9});
    transaction.begin_migration();
    transaction.mark_ready();
    int finalized = 0;
    int published = 0;
    bool failed = false;
    try {
        transaction.commit_after_success(
            [](const amr::AmrPlanScope&) -> PreparedPayload {
                throw std::runtime_error("prepare failure");
            },
            [&](const amr::AmrPlanScope&, PreparedPayload&) { ++finalized; },
            [&](PreparedPayload&&) noexcept { ++published; });
    } catch (const std::runtime_error&) {
        failed = true;
    }
    expect(failed && finalized == 0 && published == 0,
           "prepare failure crossed the physical boundary");
    expect_rejected(
        [&] {
            transaction.commit_after_success(
                [](const amr::AmrPlanScope&) { return PreparedPayload{}; },
                [](const amr::AmrPlanScope&, PreparedPayload&) {},
                [](PreparedPayload&&) noexcept {});
        },
        "failed transaction retried");
    int cleaned = 0;
    transaction.abort([&]() noexcept { ++cleaned; });
    expect(cleaned == 1
               && transaction.state() == amr::TopologyTransactionState::Aborted,
           "failed transaction did not abort exactly once");
}

void test_finalize_failure_requires_abort()
{
    amr::TopologyTransaction transaction(43, {9}, {10});
    transaction.begin_migration();
    transaction.mark_ready();
    int finalized = 0;
    int published = 0;
    bool failed = false;
    try {
        transaction.commit_after_success(
            [](const amr::AmrPlanScope&) { return PreparedPayload{3}; },
            [&](const amr::AmrPlanScope&, PreparedPayload&) {
                ++finalized;
                throw std::runtime_error("finalize failure");
            },
            [&](PreparedPayload&&) noexcept { ++published; });
    } catch (const std::runtime_error&) {
        failed = true;
    }
    expect(failed && finalized == 1 && published == 0,
           "finalize failure published staged topology");
    transaction.abort([]() noexcept {});
}

void test_move_poison_and_reentrancy()
{
    amr::TopologyTransaction source(44, {10}, {11});
    amr::TopologyTransaction moved(std::move(source));
    expect_rejected([&] { (void)source.scope(); },
                    "moved-from transaction remained usable");

    moved.begin_migration();
    moved.mark_ready();
    int reentrant_rejections = 0;
    moved.commit_after_success(
        [&](const amr::AmrPlanScope&) {
            try {
                moved.abort([]() noexcept {});
            } catch (const std::logic_error&) {
                ++reentrant_rejections;
            }
            return PreparedPayload{9};
        },
        [](const amr::AmrPlanScope&, PreparedPayload&) {},
        [](PreparedPayload&&) noexcept {});
    expect(reentrant_rejections == 1,
           "reentrant transaction mutation was accepted");
}

void test_scope_validation()
{
    expect_rejected(
        [] { amr::TopologyTransaction invalid(0, {1}, {2}); },
        "zero transaction ID accepted");
    expect_rejected(
        [] { amr::TopologyTransaction invalid(1, {2}, {2}); },
        "equal topology epochs accepted");

    amr::TopologyTransaction transaction(45, {11}, {12});
    auto plan = amr::ProlongationPlan{};
    plan.dimension = 1;
    plan.scope = {46, {11}, {12}};
    expect_rejected([&] { transaction.require_scope(plan); },
                    "wrong migration transaction accepted");
}

SimConfig regrid_config(double refine_threshold, int dimension = 1)
{
    SimConfig config{};
    config.grid.dim = dimension;
    config.grid.nblockx1 = 1;
    config.grid.nblockx2 = dimension >= 2 ? 1 : 0;
    config.grid.nblockx3 = dimension == 3 ? 1 : 0;
    config.grid.amr_max_blocks = 16;
    config.amr.lrefinemin = 0;
    config.amr.lrefinemax = 1;
    config.amr.refine_on_rho = true;
    config.amr.refine_threshold = refine_threshold;
    config.amr.derefine_threshold = 0.0;
    return config;
}

std::shared_ptr<amr::AmrTree> make_regrid_tree(
    const SimConfig& config, std::shared_ptr<amr::MemoryPool>& pool)
{
    pool = std::make_shared<amr::MemoryPool>(
        config.grid.amr_max_blocks, config.grid.dim);
    auto tree = std::make_shared<amr::AmrTree>(pool);
    tree->InitRootGrid(config, 1);
    amr::Block& root = pool->GetBlock(tree->GetActiveBlocks().front());
    std::fill(root.fluid_state.rho.begin(), root.fluid_state.rho.end(), 1.0);
    std::fill(root.fluid_state.mom_u.begin(), root.fluid_state.mom_u.end(), 2.0);
    std::fill(root.fluid_state.mom_v.begin(), root.fluid_state.mom_v.end(), 3.0);
    std::fill(root.fluid_state.mom_w.begin(), root.fluid_state.mom_w.end(), 4.0);
    std::fill(root.fluid_state.eng.begin(), root.fluid_state.eng.end(), 50.0);
    for (int index = 0; index < root.grid.GetTotalSize(); ++index) {
        root.fluid_state.enuc_rate[index] =
            0.25 * static_cast<double>(index) - 3.0;
        root.fluid_state.X(0, index) = 1.0;
    }
    root.fluid_state.rho[root.grid.GetIndex(
        root.grid.Is() + amr::BLOCK_NX / 2,
        root.grid.Js(), root.grid.Ks())] = 9.0;
    return tree;
}

void test_dimension_plan_cardinality()
{
    for (const int dimension : {2, 3}) {
        SimConfig config = regrid_config(0.01, dimension);
        std::shared_ptr<amr::MemoryPool> pool;
        auto tree = make_regrid_tree(config, pool);
        auto refinement = tree->PrepareRegrid(config);
        const int child_count = 1 << dimension;
        expect(refinement.topology_changed()
                   && static_cast<int>(
                          refinement.proposed_active_blocks().size())
                       == child_count,
               "multi-dimensional refinement cardinality drifted");
        const std::vector<amr::BlockHandle> old_handles{{{1}, {20}}};
        std::vector<amr::BlockHandle> child_handles;
        for (int child = 0; child < child_count; ++child)
            child_handles.push_back(
                {{static_cast<std::uint64_t>(child + 2)}, {21}});
        refinement.BuildMigrationPlans(
            old_handles, child_handles,
            {static_cast<std::uint64_t>(90 + dimension), {20}, {21}});
        expect(static_cast<int>(
                   refinement.prolongation_plan().operations.size())
                   == child_count * 7,
               "multi-dimensional prolongation field cardinality drifted");
        refinement.ExecuteMigration();
        refinement.ActivateForFinalization();
        refinement.PublishNoexcept();
        refinement.ReleaseRetired();

        config.amr.refine_on_rho = false;
        config.amr.refine_threshold = 1.0;
        config.amr.derefine_threshold = 1.0;
        auto restriction = tree->PrepareRegrid(config);
        expect(restriction.topology_changed()
                   && restriction.proposed_active_blocks().size() == 1,
               "multi-dimensional restriction cardinality drifted");
        const std::vector<amr::BlockHandle> parent_handle{{{15}, {22}}};
        restriction.BuildMigrationPlans(
            child_handles, parent_handle,
            {static_cast<std::uint64_t>(100 + dimension), {21}, {22}});
        expect(static_cast<int>(
                   restriction.restriction_plan().operations.size())
                   == child_count * 7,
               "multi-dimensional restriction field cardinality drifted");
        restriction.ExecuteMigration();
        restriction.ActivateForFinalization();
        restriction.PublishNoexcept();
        restriction.ReleaseRetired();
        expect(tree->GetActiveBlocks().size() == 1
                   && pool->GetNumActiveBlocks() == 1,
               "multi-dimensional refine/derefine did not round trip");
    }
}

void test_device_migration_metadata_rollback()
{
    const SimConfig config = regrid_config(0.01);
    std::shared_ptr<amr::MemoryPool> pool;
    auto tree = make_regrid_tree(config, pool);
    const auto old_active = tree->GetActiveBlocks();
    const auto old_state = pool->GetBlock(old_active.front()).fluid_state;
    auto prepared = tree->PrepareRegrid(config);
    const std::vector<amr::BlockHandle> old_handles{{{1}, {31}}};
    const std::vector<amr::BlockHandle> new_handles{{{2}, {32}}, {{3}, {32}}};
    prepared.BuildMigrationPlans(old_handles, new_handles, {71, {31}, {32}});
    expect_rejected([&] { prepared.CompleteDeviceMigration(); },
                  "device migration completed before metadata activation");
    auto incomplete = prepared.prolongation_plan();
    incomplete.operations.pop_back();
    amr::finalize_amr_plan(incomplete);
    expect_rejected([&] {
        amr::compile_regrid_execution_plan(incomplete, prepared.restriction_plan(), 1);
    }, "incomplete all-field device group was accepted");
    prepared.ActivateForDeviceMigration();
    expect(tree->GetActiveBlocks().size() == 2,
           "device migration failed to activate proposed neighbor metadata");
    expect_rejected([&] { prepared.ExecuteMigration(); },
                  "Host migration ran after device metadata activation");
    expect(pool->GetBlock(old_active.front()).fluid_state.rho == old_state.rho,
           "device topology activation wrote old accepted fields");
    prepared.AbortNoexcept();
    expect(tree->GetActiveBlocks() == old_active && pool->GetNumActiveBlocks() == 1,
           "failed device migration did not restore topology/allocation ownership");
    expect(pool->GetBlock(old_active.front()).fluid_state.rho == old_state.rho,
           "device migration rollback changed old accepted fields");
}

void test_prepared_regrid_commit_and_abort()
{
    const SimConfig config = regrid_config(0.01);
    std::shared_ptr<amr::MemoryPool> pool;
    auto tree = make_regrid_tree(config, pool);
    const int old_id = tree->GetActiveBlocks().front();
    const auto old_rho = pool->GetBlock(old_id).fluid_state.rho;
    const auto old_enuc = pool->GetBlock(old_id).fluid_state.enuc_rate;

    auto prepared = tree->PrepareRegrid(config);
    expect(prepared.topology_changed(),
           "refinement fixture did not stage a topology change");
    expect(tree->GetActiveBlocks() == std::vector<int>{old_id}
               && pool->GetNumActiveBlocks() == 3,
           "regrid preparation published its staged topology");
    expect(prepared.proposed_active_blocks().size() == 2,
           "1D refinement did not stage two children");

    const std::vector<amr::BlockHandle> old_handles{{{1}, {10}}};
    const std::vector<amr::BlockHandle> new_handles{
        {{2}, {11}}, {{3}, {11}}};
    const amr::AmrPlanScope scope{71, {10}, {11}};
    prepared.BuildMigrationPlans(old_handles, new_handles, scope);
    expect(prepared.prolongation_plan().operations.size() == 14
               && prepared.restriction_plan().operations.empty(),
           "refinement migration plan field coverage drifted");
    prepared.ExecuteMigration();
    expect(tree->GetActiveBlocks() == std::vector<int>{old_id},
           "migration execution published staged children");
    prepared.ActivateForFinalization();
    expect(tree->GetActiveBlocks().size() == 2,
           "staged topology did not activate for finalization");
    prepared.PublishNoexcept();
    prepared.ReleaseRetired();
    expect(pool->GetNumActiveBlocks() == 2,
           "committed refinement did not retire its parent");
    for (const int child_id : tree->GetActiveBlocks()) {
        const auto& child = pool->GetBlock(child_id);
        expect(child.level == 1
                   && child.fluid_state.rho[child.grid.GetIndex(
                       child.grid.Is(), child.grid.Js(), child.grid.Ks())]
                       > 0.0,
                "plan-driven prolongation did not initialize a child");
        expect(std::any_of(
                   child.fluid_state.enuc_rate.begin(),
                   child.fluid_state.enuc_rate.end(),
                   [](double value) { return value != 0.0; }),
               "plan-driven prolongation did not initialize ENUC");
    }

    SimConfig derefine = config;
    derefine.amr.refine_on_rho = false;
    derefine.amr.refine_threshold = 1.0;
    derefine.amr.derefine_threshold = 1.0;
    auto restriction = tree->PrepareRegrid(derefine);
    expect(restriction.topology_changed()
               && restriction.proposed_active_blocks().size() == 1,
           "1D derefinement did not stage one parent");
    const std::vector<amr::BlockHandle> parent_handle{{{4}, {12}}};
    const amr::AmrPlanScope restriction_scope{72, {11}, {12}};
    restriction.BuildMigrationPlans(
        new_handles, parent_handle, restriction_scope);
    expect(restriction.prolongation_plan().operations.empty()
               && restriction.restriction_plan().operations.size() == 14,
           "derefinement migration plan field coverage drifted");
    restriction.ExecuteMigration();
    restriction.ActivateForFinalization();
    restriction.PublishNoexcept();
    restriction.ReleaseRetired();
    expect(tree->GetActiveBlocks().size() == 1
               && pool->GetNumActiveBlocks() == 1,
           "committed derefinement did not retire its children");
    const int parent_id = tree->GetActiveBlocks().front();
    const auto parent_rho = pool->GetBlock(parent_id).fluid_state.rho;
    const auto& parent_enuc =
        pool->GetBlock(parent_id).fluid_state.enuc_rate;
    expect(*std::max_element(parent_rho.begin(), parent_rho.end()) > 1.0,
           "plan-driven restriction did not average child state");
    for (int k = pool->GetBlock(parent_id).grid.Ks();
         k < pool->GetBlock(parent_id).grid.Ke(); ++k) {
        for (int j = pool->GetBlock(parent_id).grid.Js();
             j < pool->GetBlock(parent_id).grid.Je(); ++j) {
            for (int i = pool->GetBlock(parent_id).grid.Is();
                 i < pool->GetBlock(parent_id).grid.Ie(); ++i) {
                const int index =
                    pool->GetBlock(parent_id).grid.GetIndex(i, j, k);
                expect(std::abs(parent_enuc[index] - old_enuc[index]) < 1e-12,
                       "ENUC did not survive refine/restrict round trip");
            }
        }
    }

    SimConfig no_change = derefine;
    no_change.amr.derefine_threshold = -1.0;
    auto unchanged = tree->PrepareRegrid(no_change);
    const auto unchanged_active = unchanged.proposed_active_blocks();
    expect(!unchanged.topology_changed()
               && unchanged_active.size() == tree->GetActiveBlocks().size()
               && std::equal(
                   unchanged_active.begin(), unchanged_active.end(),
                   tree->GetActiveBlocks().begin()),
           "no-change regrid staged a different active topology");
    unchanged.PublishNoChangeNoexcept();
    expect(tree->GetActiveBlocks() == std::vector<int>{parent_id}
               && pool->GetBlock(parent_id).fluid_state.rho == parent_rho,
           "no-change regrid changed topology or state");

    std::shared_ptr<amr::MemoryPool> rollback_pool;
    auto rollback_tree = make_regrid_tree(config, rollback_pool);
    const int rollback_id = rollback_tree->GetActiveBlocks().front();
    const auto rollback_rho = rollback_pool->GetBlock(
        rollback_id).fluid_state.rho;
    auto rollback = rollback_tree->PrepareRegrid(config);
    rollback.BuildMigrationPlans(old_handles, new_handles, scope);
    rollback.ExecuteMigration();
    rollback.ActivateForFinalization();
    rollback.AbortNoexcept();
    expect(rollback_tree->GetActiveBlocks() == std::vector<int>{rollback_id}
               && rollback_pool->GetNumActiveBlocks() == 1
               && rollback_pool->GetBlock(rollback_id).fluid_state.rho
                   == rollback_rho,
           "aborted staged regrid did not restore the old hierarchy");
    expect(old_rho == rollback_rho,
           "independent regrid fixtures did not start byte-equivalent");
}

std::vector<arch::topology::TopologyObservation> observe_tree(
    const amr::AmrTree& tree, const amr::MemoryPool& pool, int dimension)
{
    std::vector<arch::topology::TopologyObservation> observations;
    observations.reserve(tree.GetActiveBlocks().size());
    for (const int pool_index : tree.GetActiveBlocks()) {
        const amr::Block& block = pool.GetBlock(pool_index);
        observations.push_back({
            pool_index,
            {dimension, block.level, block.logical_x1,
             block.logical_x2, block.logical_x3}});
    }
    return observations;
}

void test_staged_allocation_failure_rollback()
{
    using arch::topology::TopologyDomainBounds;
    using arch::topology::TopologyIdentityRegistry;

    const auto refinement_flags = [](const amr::AmrTree& tree,
                                     const amr::MemoryPool& pool) {
        std::vector<int> result;
        result.reserve(tree.GetActiveBlocks().size());
        for (const int id : tree.GetActiveBlocks())
            result.push_back(pool.GetBlock(id).refine_flag);
        return result;
    };
    const auto active_states = [](const amr::AmrTree& tree,
                                  const amr::MemoryPool& pool) {
        std::vector<std::vector<double>> result;
        result.reserve(tree.GetActiveBlocks().size());
        for (const int id : tree.GetActiveBlocks())
            result.push_back(pool.GetBlock(id).fluid_state.rho);
        return result;
    };

    const auto exercise_failure = [&] (
        const SimConfig& config, const std::shared_ptr<amr::AmrTree>& tree,
        const std::shared_ptr<amr::MemoryPool>& pool,
        TopologyIdentityRegistry& registry, const std::string& context) {
        const std::vector<int> active_before = tree->GetActiveBlocks();
        const int count_before = pool->GetNumActiveBlocks();
        const auto flags_before = refinement_flags(*tree, *pool);
        const auto states_before = active_states(*tree, *pool);
        const amr::TopologyEpoch epoch_before = registry.epoch();
        int guarded_id = -1;
        bool saw_guarded_active = false;
        bool failed = false;
        try {
            auto unexpected = tree->PrepareRegrid(
                config, {},
                [&](const amr::AmrTree&, int allocated_id) {
                    guarded_id = allocated_id;
                    saw_guarded_active = pool->GetBlock(allocated_id).active;
                    throw InjectedStagedAllocationFailure{};
                });
            (void)unexpected;
        } catch (const InjectedStagedAllocationFailure&) {
            failed = true;
        }

        expect(failed && saw_guarded_active && guarded_id >= 0,
               context + " did not inject after pool allocation");
        expect(tree->GetActiveBlocks() == active_before
                   && pool->GetNumActiveBlocks() == count_before
                   && !pool->GetBlock(guarded_id).active,
               context + " leaked its guarded pool block");
        expect(refinement_flags(*tree, *pool) == flags_before
                   && active_states(*tree, *pool) == states_before,
               context + " changed the committed hierarchy state");
        expect(registry.epoch() == epoch_before,
               context + " changed the committed topology epoch");
        registry.validate_committed_snapshot(observe_tree(*tree, *pool, 1));

        // The freed id must return to the same pool stack position so a retry
        // observes the same first allocation without publishing it.
        auto retry = tree->PrepareRegrid(config);
        expect(retry.topology_changed()
                   && std::find(retry.proposed_active_blocks().begin(),
                                retry.proposed_active_blocks().end(), guarded_id)
                       != retry.proposed_active_blocks().end(),
               context + " did not restore the pool allocation order");
        retry.AbortNoexcept();
        expect(tree->GetActiveBlocks() == active_before
                   && pool->GetNumActiveBlocks() == count_before
                   && registry.epoch() == epoch_before,
               context + " retry rollback drifted committed ownership");
        registry.validate_committed_snapshot(observe_tree(*tree, *pool, 1));
    };

    {
        const SimConfig config = regrid_config(0.01);
        std::shared_ptr<amr::MemoryPool> pool;
        auto tree = make_regrid_tree(config, pool);
        TopologyIdentityRegistry registry(
            TopologyDomainBounds{1, {1U, 1U, 1U}, 1});
        auto adoption = registry.stage_adoption(observe_tree(*tree, *pool, 1));
        registry.commit(std::move(adoption));
        exercise_failure(config, tree, pool, registry,
                         "refinement allocation failure");
    }

    {
        const SimConfig refine = regrid_config(0.01);
        std::shared_ptr<amr::MemoryPool> pool;
        auto tree = make_regrid_tree(refine, pool);
        expect(tree->Regrid(refine),
               "restriction failure fixture did not refine");

        SimConfig derefine = refine;
        derefine.amr.refine_on_rho = false;
        derefine.amr.refine_threshold = 1.0;
        derefine.amr.derefine_threshold = 1.0;
        TopologyIdentityRegistry registry(
            TopologyDomainBounds{1, {1U, 1U, 1U}, 1});
        auto adoption = registry.stage_adoption(observe_tree(*tree, *pool, 1));
        registry.commit(std::move(adoption));
        exercise_failure(derefine, tree, pool, registry,
                         "restriction allocation failure");
    }
}

void expect_same_coherence(const arch::state::SlotCoherence& actual,
                           const arch::state::SlotCoherence& expected,
                           const std::string& message)
{
    const auto same_region = [](const auto& lhs, const auto& rhs) {
        return lhs.residency == rhs.residency
            && lhs.version == rhs.version
            && lhs.completion == rhs.completion
            && lhs.pending_transfer == rhs.pending_transfer;
    };
    expect(same_region(actual.interior, expected.interior)
               && same_region(actual.ghost, expected.ghost)
               && actual.ghost_source_version
                   == expected.ghost_source_version,
           message);
}

void test_identity_and_residency_rollback()
{
    using arch::scheduler::MonotonicSchedulerClock;
    using arch::state::StateKey;
    using arch::state::StateResidencyLedger;
    using arch::state::StateSlot;
    using arch::topology::TopologyDomainBounds;
    using arch::topology::TopologyIdentityRegistry;

    const SimConfig config = regrid_config(0.01);
    std::shared_ptr<amr::MemoryPool> pool;
    auto tree = make_regrid_tree(config, pool);
    const int old_id = tree->GetActiveBlocks().front();
    const auto old_state = pool->GetBlock(old_id).fluid_state.rho;

    TopologyIdentityRegistry registry(TopologyDomainBounds{
        1, {1U, 1U, 1U}, 1});
    MonotonicSchedulerClock clock;
    auto adoption = registry.stage_adoption(observe_tree(*tree, *pool, 1));
    std::unique_ptr<StateResidencyLedger> ledger;
    std::vector<amr::BlockHandle> old_handles;
    const auto initial = registry.commit_after_success(
        std::move(adoption), [&](const auto& proposed) {
            ledger = std::make_unique<StateResidencyLedger>(proposed.epoch);
            const auto witness = clock.next_publication();
            for (const auto handle : proposed.handles_in_observation_order)
                ledger->register_block(
                    handle, witness.version, witness.completion);
            old_handles = proposed.handles_in_observation_order;
        });
    expect(initial.handles_in_observation_order == old_handles
               && old_handles.size() == 1,
           "initial rollback identity adoption drifted");
    const auto old_coherence = ledger->inspect(
        StateKey{old_handles.front(), StateSlot::Current});

    auto prepared = tree->PrepareRegrid(config);
    std::vector<arch::topology::TopologyObservation> proposed_observations;
    for (const int pool_index : prepared.proposed_active_blocks()) {
        const amr::Block& block = pool->GetBlock(pool_index);
        proposed_observations.push_back({
            pool_index,
            {1, block.level, block.logical_x1,
             block.logical_x2, block.logical_x3}});
    }
    auto candidate = registry.stage_reconciliation(proposed_observations);
    const auto proposed = candidate.reconciliation();
    const amr::AmrPlanScope scope{
        83, old_handles.front().epoch, proposed.epoch};
    prepared.BuildMigrationPlans(
        old_handles, proposed.handles_in_observation_order, scope);

    amr::TopologyTransaction transaction(
        scope.transaction_id, scope.from_epoch, scope.to_epoch);
    transaction.begin_migration();
    transaction.require_scope(prepared.prolongation_plan());
    transaction.require_scope(prepared.restriction_plan());
    prepared.ExecuteMigration();
    transaction.mark_ready();

    bool failed = false;
    try {
        (void)registry.commit_after_success(
            std::move(candidate), [&](const auto& committed_topology) {
                transaction.commit_after_success(
                    [&](const amr::AmrPlanScope&) {
                        struct Payload {
                            std::unique_ptr<StateResidencyLedger> ledger;
                        };
                        Payload payload;
                        payload.ledger =
                            std::make_unique<StateResidencyLedger>(
                                committed_topology.epoch);
                        const auto witness = clock.next_publication();
                        for (const auto handle
                             : committed_topology
                                   .handles_in_observation_order) {
                            payload.ledger->register_block(
                                handle, witness.version,
                                witness.completion);
                        }
                        return payload;
                    },
                    [&](const amr::AmrPlanScope&, auto&) {
                        prepared.ActivateForFinalization();
                        throw std::runtime_error(
                            "injected staged ghost failure");
                    },
                    [](auto&&) noexcept {});
            });
    } catch (const std::runtime_error&) {
        failed = true;
    }
    expect(failed, "injected regrid finalizer failure was not observed");
    transaction.abort([&]() noexcept { prepared.AbortNoexcept(); });

    expect(tree->GetActiveBlocks() == std::vector<int>{old_id}
               && pool->GetNumActiveBlocks() == 1
               && pool->GetBlock(old_id).fluid_state.rho == old_state,
           "transaction abort did not restore the old physical hierarchy");
    registry.validate_committed_snapshot(observe_tree(*tree, *pool, 1));
    expect_same_coherence(
        ledger->inspect(StateKey{old_handles.front(), StateSlot::Current}),
        old_coherence,
        "transaction abort changed the old residency ledger");
}

} // namespace

int main()
{
    try {
        test_success_order();
        test_prepare_failure_requires_abort();
        test_finalize_failure_requires_abort();
        test_move_poison_and_reentrancy();
        test_scope_validation();
        test_dimension_plan_cardinality();
        test_device_migration_metadata_rollback();
        test_prepared_regrid_commit_and_abort();
        test_staged_allocation_failure_rollback();
        test_identity_and_residency_rollback();
        std::cout << "TOPOLOGY_TRANSACTION_PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
