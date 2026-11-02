/**
 * @file test_topology_ownership.cpp
 * @brief Exercise published ownership with five mixed-level leaves.
 *
 * This is a topology/identity contract, not a fluid migration or reflux test.
 * Two-rank runs use the exact metadata collector called by DriverRuntime.
 */
#include "driver/runtime/TopologyIdentityRegistry.h"

#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif
#include <algorithm>
#include <iostream>
#include <string>

namespace {
using namespace arch::topology;
const TopologyDomainBounds bounds{2, {2, 1, 1}, 1};
const std::vector<LogicalBlockIdentity> five{
    {2, 0, 0, 0, 0}, {2, 1, 2, 0, 0}, {2, 1, 2, 1, 0},
    {2, 1, 3, 0, 0}, {2, 1, 3, 1, 0}};
int world_rank = 0, world_size = 1;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template<class Function>
void rejects(Function&& function, const char* message)
{
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
int owner(std::size_t index, bool moved)
{
    return index == 0 || (moved && index == 1) ? 0 : 1;
}
std::vector<TopologyObservation> local_five(int rank, bool moved = false)
{
    std::vector<TopologyObservation> local;
    for (std::size_t i = 0; i < five.size(); ++i)
        if (owner(i, moved) == rank)
            local.push_back({static_cast<int>(local.size()) * 3, five[i], rank});
    std::reverse(local.begin(), local.end());
    return local;
}
std::vector<TopologyObservation> collect(
    const std::vector<TopologyObservation>& local, int rank, bool moved = false)
{
    if (world_size == 2)
        return collect_topology_observations(local, rank, world_size);
    // MPI-OFF still verifies both registry perspectives, without simulating
    // transport success. Actual transport is covered only by the two-rank run.
    auto result = local;
    for (auto observation : local_five(1 - rank, moved)) {
        observation.pool_index = -1;
        result.push_back(observation);
    }
    return result;
}

void registry_contract(int rank)
{
    TopologyIdentityRegistry registry(bounds, rank, 2);
    auto local = local_five(rank);
    auto global = collect(local, rank);
    require(global.size() == five.size(), "global metadata count differs");
    for (const auto& record : global)
        require((record.owner_rank == rank) == (record.pool_index >= 0),
                "remote metadata acquired a local pool slot");
    auto adoption = registry.stage_adoption(global);
    const auto initial = adoption.reconciliation();
    require(initial.handles_in_observation_order.size() == local.size(),
            "remote handles entered the local residency list");
    registry.commit(std::move(adoption));
    registry.validate_committed_snapshot(local);
    for (std::size_t i = 0; i < five.size(); ++i) {
        require(registry.handle_for(five[i]).uid.value == i + 1,
                "UID depends on process-local order or pool index");
        require(registry.owner_for(five[i]) == owner(i, false), "wrong initial owner");
    }
    for (std::size_t i = 0; i < local.size(); ++i)
        require(registry.handle_for_pool(local[i].pool_index) ==
                    initial.handles_in_observation_order[i],
                "local slot lowering differs from observation order");

    // Rebinding/reordering only local slots must preserve logical identity.
    auto rebound = local;
    for (auto& item : rebound) item.pool_index += 20;
    auto same = registry.stage_reconciliation(collect(rebound, rank));
    require(!same.reconciliation().topology_changed, "slot move changed topology");
    require(same.reconciliation().epoch == initial.epoch, "slot move changed epoch");
    registry.commit(std::move(same));
    registry.validate_committed_snapshot(rebound);
    rejects([&] { registry.validate_committed_snapshot(local); },
            "stale local lowering was accepted");
    for (const auto& item : local)
        rejects([&] { (void)registry.handle_for_pool(item.pool_index); },
                "old pool slot remained bound");

    auto moved_local = local_five(rank, true);
    auto moved_global = collect(moved_local, rank, true);
    auto failed = registry.stage_reconciliation(moved_global);
    require(failed.reconciliation().topology_changed, "ownership-only move was invisible");
    require(failed.reconciliation().added.empty() && failed.reconciliation().retired.empty(),
            "ownership move replaced persistent identities");
    auto stale = registry.stage_reconciliation(moved_global);
    rejects([&] {
        registry.commit_after_success(std::move(failed), [](const auto&) {
            throw std::runtime_error("injected local finalizer failure");
        });
    }, "failed finalization published a candidate");
    require(registry.owner_for(five[1]) == 1 && registry.epoch() == initial.epoch,
            "failed candidate changed committed metadata");
    registry.validate_committed_snapshot(rebound);

    auto move = registry.stage_reconciliation(moved_global);
    const auto moved = move.reconciliation();
    registry.commit(std::move(move));
    require(registry.owner_for(five[1]) == 0, "migrated owner not published");
    require(moved.epoch.value == initial.epoch.value + 1, "global epoch did not advance once");
    for (std::size_t i = 0; i < five.size(); ++i) {
        require(registry.handle_for(five[i]).uid.value == i + 1, "migration changed UID");
        require(registry.handle_for(five[i]).epoch == moved.epoch, "survivor retained stale epoch");
    }
    registry.validate_committed_snapshot(moved_local);
    rejects([&] { registry.commit(std::move(stale)); }, "stale candidate was accepted");
    rejects([&] { registry.validate_committed_snapshot(rebound); }, "old ownership was accepted");

    auto returned = registry.stage_reconciliation(collect(local, rank));
    registry.commit(std::move(returned));
    require(registry.owner_for(five[1]) == 1, "ownership did not return");
    registry.validate_committed_snapshot(local);

    // A real leaf replacement retires children and allocates a fresh parent UID.
    const LogicalBlockIdentity right_root{2, 0, 1, 0, 0};
    std::vector<TopologyObservation> coarse_local{
        {7, rank == 0 ? five[0] : right_root, rank}};
    std::vector<TopologyObservation> coarse_global;
    if (world_size == 2)
        coarse_global = collect_topology_observations(coarse_local, rank, 2);
    else
        coarse_global = {{rank == 0 ? 7 : -1, five[0], 0},
                         {rank == 1 ? 7 : -1, right_root, 1}};
    auto coarse = registry.stage_reconciliation(coarse_global);
    require(coarse.reconciliation().retired.size() == 4, "coarsening lost child retirement");
    registry.commit(std::move(coarse));
    require(registry.handle_for(five[0]).uid.value == 1, "surviving coarse UID changed");
    require(registry.handle_for(right_root).uid.value == 6, "retired UID was reused");
    for (std::uint64_t uid = 2; uid <= 5; ++uid)
        require(registry.is_retired({uid}), "retired child still live");
    registry.validate_committed_snapshot(coarse_local);

    auto missing = global;
    missing.erase(std::find_if(missing.begin(), missing.end(),
        [](const auto& observation) { return observation.identity == five.back(); }));
    rejects([&] { (void)registry.stage_reconciliation(missing); }, "missing leaf accepted");
    auto duplicate = global;
    duplicate.push_back(global.front());
    rejects([&] { (void)registry.stage_reconciliation(duplicate); }, "duplicate ownership accepted");
    auto overlap = global;
    overlap.push_back({rank == 1 ? 99 : -1, right_root, 1});
    rejects([&] { (void)registry.stage_reconciliation(overlap); }, "parent/child overlap accepted");
    auto wrong_storage = global;
    auto remote = std::find_if(wrong_storage.begin(), wrong_storage.end(),
        [rank](const auto& observation) { return observation.owner_rank != rank; });
    remote->pool_index = 99;
    rejects([&] { (void)registry.stage_reconciliation(wrong_storage); },
            "remote block accepted local storage");
    require(registry.handle_for(right_root).uid.value == 6, "rejection changed live registry");

    TopologyIdentityRegistry first(bounds, rank, 2), second(bounds, rank, 2);
    auto foreign = first.stage_adoption(global);
    rejects([&] { second.commit(std::move(foreign)); }, "foreign candidate accepted");
}

void transport_contract()
{
    std::vector<TopologyObservation> all;
    for (std::size_t i = 0; i < five.size(); ++i)
        all.push_back({static_cast<int>(i), five[i], 0});
    TopologyIdentityRegistry serial(bounds);
    auto candidate = serial.stage_adoption(all);
    serial.commit(std::move(candidate));
    for (std::size_t i = 0; i < five.size(); ++i)
        require(serial.handle_for(five[i]).uid.value == i + 1,
                "serial and distributed identity assignments differ");
    if (world_size == 1) {
        const auto gathered = collect_topology_observations(all, 0, 1);
        require(gathered.size() == all.size(), "serial collector changed metadata");
        rejects([&] { (void)collect_topology_observations(all, 0, 2); },
                "unavailable two-rank communicator accepted");
        return;
    }
    // Metadata must represent a rank with no owned leaves. This does not lift
    // the application's existing fixed-grid nonempty-partition restriction.
    std::vector<TopologyObservation> one_owner;
    if (world_rank == 1) {
        one_owner = all;
        for (auto& record : one_owner) record.owner_rank = 1;
    }
    TopologyIdentityRegistry empty_rank_registry(bounds, world_rank, 2);
    auto empty_rank_candidate = empty_rank_registry.stage_adoption(
        collect_topology_observations(one_owner, world_rank, 2));
    require(empty_rank_candidate.reconciliation().handles_in_observation_order.size()
                == one_owner.size(), "empty rank acquired local handles");
    empty_rank_registry.commit(std::move(empty_rank_candidate));
    empty_rank_registry.validate_committed_snapshot(one_owner);
    for (std::size_t index = 0; index < five.size(); ++index)
        require(empty_rank_registry.owner_for(five[index]) == 1
                    && empty_rank_registry.handle_for(five[index]).uid.value == index + 1,
                "empty rank lost global identity or ownership");

    auto local = local_five(world_rank);
    if (world_rank == 1) local.front().owner_rank = 0;
    rejects([&] { (void)collect_topology_observations(local, world_rank, 2); },
            "one-rank owner spoof was not rejected on every rank");
    local = local_five(world_rank);
    if (world_rank == 1) local.front().pool_index = -1;
    rejects([&] { (void)collect_topology_observations(local, world_rank, 2); },
            "one-rank invalid slot was not rejected on every rank");
    local = local_five(world_rank);
    rejects([&] {
        (void)collect_topology_observations(local, world_rank,
                                          world_rank == 0 ? 1 : 2);
    }, "rank-count mismatch escaped collective rejection");
}
} // namespace

int main(int argc, char** argv)
{
#if ARCH_MPI_ENABLED
    int provided = 0;
    if (MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided) != MPI_SUCCESS)
        return 1;
    if (provided < MPI_THREAD_FUNNELED) { MPI_Abort(MPI_COMM_WORLD, 1); return 1; }
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
#else
    (void)argc; (void)argv;
#endif
    try {
        require(world_size == 1 || world_size == 2, "test requires one or two ranks");
        if (world_size == 2) registry_contract(world_rank);
        else { registry_contract(0); registry_contract(1); }
        transport_contract();
        if (world_rank == 0)
            std::cout << "topology ownership passed; ranks=" << world_size
                      << "; five-leaf metadata only, no AMR fluid support claimed\n";
    } catch (const std::exception& error) {
        std::cerr << "rank " << world_rank << ": " << error.what() << '\n';
#if ARCH_MPI_ENABLED
        MPI_Abort(MPI_COMM_WORLD, 1);
#endif
        return 1;
    }
#if ARCH_MPI_ENABLED
    MPI_Finalize();
#endif
    return 0;
}
