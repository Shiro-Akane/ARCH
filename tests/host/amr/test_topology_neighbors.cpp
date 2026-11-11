/**
 * @file test_topology_neighbors.cpp
 * @brief Compare partitioned mixed-level adjacency and existing transfer recipes.
 * Local refinement initializes the fixture; no remote fluid exchange, reflux,
 * automatic distributed refinement or payload migration is claimed.
 */
#include "amr/AMRControl.h"
#include "driver/runtime/TopologyIdentityRegistry.h"
#include "fixtures/amr/partitioned_topology_fixture.h"
#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif
#include <iostream>
#include <memory>
#include <set>
#include <string>

namespace {
using namespace arch::topology;
int world_rank = 0, world_size = 1;
void require(bool okay, const char* message) {
    if (!okay)
        throw std::runtime_error(message);
}
template <class Function>
void rejects(Function&& function, const char* message, const char* expected_error = nullptr) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception& error) {
        require(expected_error == nullptr ||
                    std::string(error.what()).find(expected_error) != std::string::npos,
                "rejection came from an unrelated failure");
        rejected = true;
    }
    require(rejected, message);
}
using Fixture = amr::test::PartitionedTopologyFixture;

void check_case(int dim, bool periodic) {
    Fixture serial(dim, 0, 1, periodic);
    if (world_size == 1) {
        require(serial.handles.size() == 1u + (1u << dim), "serial mixed-level fixture size");
        (void)serial.control.ghost_exchange.GetPlans(serial.control.pool, serial.control.tree, dim,
                                                     serial.handles);
        return;
    }
    Fixture distributed(dim, world_rank, 2, periodic);
    auto& c = distributed.control;
    std::set<amr::LogicalBlockKey> local_keys;
    int remote_faces = 0, local_faces = 0;
    for (int id : c.tree->GetActiveBlocks()) {
        const auto& block = c.pool->GetBlock(id);
        const amr::LogicalBlockKey key{dim, block.level, block.logical_x1, block.logical_x2,
                                       block.logical_x3};
        local_keys.insert(key);
        const int reference_id = serial.control.tree->FindBlock(key.level, key.logical_x1,
                                                                key.logical_x2, key.logical_x3);
        require(reference_id >= 0, "partition leaf absent from serial reference");
        const auto& reference = serial.control.pool->GetBlock(reference_id);
        for (int face = 0; face < 6; ++face) {
            const auto& actual = block.face_neighbors[face];
            const auto& expected = reference.face_neighbors[face];
            require(actual.count == expected.count && actual.level_diff == expected.level_diff,
                    "partition face cardinality or level differs");
            require(block.grid.amr_coarse_fine_face[face] ==
                        reference.grid.amr_coarse_fine_face[face],
                    "coarse-fine face flag differs");
            for (int n = 0; n < actual.count; ++n) {
                const auto& peer = actual.blocks[n];
                require(peer.logical == expected.blocks[n].logical &&
                            peer.handle == expected.blocks[n].handle,
                        "partition neighbor identity or ordering differs");
                const int owner = peer.logical.level == 0 ? 0 : 1;
                require(peer.owner_rank == owner, "wrong neighbor owner");
                require((peer.pool_index >= 0) == (owner == world_rank),
                        "remote neighbor has local storage");
                if (owner == world_rank)
                    ++local_faces;
                else
                    ++remote_faces;
            }
        }
    }
    require(remote_faces > 0, "fixture did not exercise remote neighbors");
    if (world_rank == 1 && dim >= 2)
        require(local_faces > 0, "local and remote neighbors did not coexist");
    const auto& plans = c.ghost_exchange.GetPlans(c.pool, c.tree, dim, distributed.handles);
    auto expected = serial.control.ghost_exchange.BuildCoarseFinePlan(
        serial.control.pool, serial.control.tree, dim, serial.handles);
    std::erase_if(expected.operations, [&](const auto& operation) {
        return !local_keys.contains(operation.destination.logical);
    });
    amr::finalize_amr_plan(expected);
    require(plans.coarse_fine.operations == expected.operations,
            "distributed coarse-fine recipe differs from serial");
    require(!plans.coarse_fine.operations.empty(), "missing coarse-fine operations");
    const auto actual_cells = amr::compile_coarse_fine_cell_plan(plans.coarse_fine, 2);
    const auto expected_cells = amr::compile_coarse_fine_cell_plan(expected, 2);
    require(actual_cells.transfers.size() == expected_cells.transfers.size(),
            "cell plan extent differs");
    for (std::size_t i = 0; i < actual_cells.transfers.size(); ++i) {
        const auto& a = actual_cells.transfers[i];
        const auto& b = expected_cells.transfers[i];
        require(a.source == b.source && a.destination == b.destination &&
                    a.destination_cell == b.destination_cell && a.source_cells == b.source_cells &&
                    a.slope_cells == b.slope_cells && a.fine_position == b.fine_position &&
                    a.source_count == b.source_count && a.rule == b.rule,
                "cell recipe changed across partition");
    }
    // Populate all slots; a malformed plan on one rank must reject collectively.
    for (int id : c.tree->GetActiveBlocks()) {
        auto& block = c.pool->GetBlock(id);
        block.state_next = block.fluid_state;
        block.state_scratch = block.fluid_state;
    }
    // Failure must preserve every field and species in every state slot.
    const std::array<FluidState amr::Block::*, 3> slots{
        &amr::Block::fluid_state, &amr::Block::state_next, &amr::Block::state_scratch};
    const auto snapshot = [&] {
        std::vector<std::vector<double>> fields;
        for (int id : c.tree->GetActiveBlocks()) {
            const auto& block = c.pool->GetBlock(id);
            for (const auto slot : slots) {
                const auto& state = block.*slot;
                for (const auto* data : amr::test::regrid_fields(state))
                    fields.emplace_back(data, data + state.rho.size());
                fields.push_back(state.mass_fractions);
            }
        }
        return fields;
    };
    const auto before = snapshot();
    auto invalid_plan = plans.coarse_fine;
    if (world_rank == 0)
        invalid_plan.operations.front().source.handle.epoch.value += 1;
    for (const auto slot : slots) {
        rejects(
            [&] {
                c.ghost_exchange.ExecuteCoarseFinePlan(invalid_plan, c.pool, c.tree, dim, slot,
                                                       distributed.handles);
            },
            "stale remote AMR plan unexpectedly accepted");
        require(snapshot() == before, "rejected remote execution wrote destination");
    }
    c.flux_register.EnsureSpecies(2);
    (void)c.RequireFluxTopologyPlan(2);
    rejects([&] { c.ApplyReflux(.01); }, "reflux accepted without completed collective stages",
            "MPI reflux requires the collective stage boundary");
    require(snapshot() == before, "rejected reflux wrote destination");
    auto descriptors = distributed.registry.blocks();
    auto remote = std::find_if(descriptors.begin(), descriptors.end(),
                               [](const auto& leaf) { return leaf.owner_rank != world_rank; });
    remote->handle.epoch.value += 1;
    rejects([&] { c.tree->UpdateNeighbors(distributed.config, descriptors); },
            "stale global descriptor accepted");
    const auto builds = c.ghost_exchange.PlanCacheBuilds();
    (void)c.ghost_exchange.GetPlans(c.pool, c.tree, dim, distributed.handles);
    require(c.ghost_exchange.PlanCacheBuilds() == builds, "failed refresh replaced neighbor graph");

    if (world_rank == 0) {
        // Refine a remote interface child again: coverage stays complete, but
        // the root/grandchild face violates 2:1 and must not publish.
        auto unbalanced = distributed.registry.blocks();
        auto child = std::find_if(unbalanced.begin(), unbalanced.end(), [](const auto& leaf) {
            return leaf.logical.level == 1 && leaf.logical.logical_x1 == 2 &&
                   leaf.logical.logical_x2 == 0 && leaf.logical.logical_x3 == 0;
        });
        const auto parent = *child;
        unbalanced.erase(child);
        for (int index = 0; index < (1 << dim); ++index)
            unbalanced.push_back(
                {{dim, 2, parent.logical.logical_x1 * 2 + static_cast<unsigned>(index & 1),
                  dim >= 2 ? static_cast<unsigned>((index >> 1) & 1) : 0u,
                  dim == 3 ? static_cast<unsigned>((index >> 2) & 1) : 0u},
                 {{100u + static_cast<unsigned>(index)}, distributed.registry.epoch()},
                 1,
                 -1});
        bool rejected_balance = false;
        try {
            c.tree->UpdateNeighbors(distributed.config, unbalanced);
        } catch (const std::invalid_argument& error) {
            rejected_balance = std::string(error.what()).find("2:1") != std::string::npos;
        }
        require(rejected_balance, "unbalanced global interface was not rejected");
    }
}
} // namespace
int main(int argc, char** argv) {
#if ARCH_MPI_ENABLED
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED) {
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
#else
    (void)argc;
    (void)argv;
#endif
    try {
        require(world_size == 1 || world_size == 2, "requires one or two ranks");
        for (int invalid_dimension : {0, 4}) {
            SimConfig invalid;
            invalid.grid.dim = invalid_dimension;
            invalid.grid.nblockx1 = 1;
            invalid.grid.nblockx2 = invalid.grid.nblockx3 = 0;
            invalid.amr.lrefinemin = invalid.amr.lrefinemax = 0;
            amr::AMRControl control(2, 1);
            rejects([&] { control.tree->InitRootGrid(invalid, 2); },
                    "invalid face traversal dimension accepted");
        }
        for (int dim = 1; dim <= 3; ++dim)
            for (bool periodic : {false, true})
                check_case(dim, periodic);
        if (world_rank == 0)
            std::cout << "BlockTopology bytes=" << sizeof(amr::BlockTopology)
                      << ", FaceNeighbors bytes=" << sizeof(amr::Block::FaceNeighbors) << "\n"
                      << "topology neighbors passed; 1D/2D/3D, mixed levels, periodic/nonperiodic; "
                         "no remote fluid execution claimed\n";
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
}
