/**
 * @file partitioned_topology_fixture.h
 * @brief Native adoption, local refinement and global adjacency shared by MPI tests.
 */
#pragma once
#include "amr/AMRControl.h"
#include "driver/runtime/TopologyIdentityRegistry.h"
#include "fixtures/amr/regrid_migration_fixture.h"

namespace amr::test {
using arch::topology::collect_topology_observations;
using arch::topology::TopologyIdentityRegistry;
using arch::topology::TopologyObservation;
struct PartitionedTopologyFixture {
    SimConfig config;
    amr::AMRControl control;
    TopologyIdentityRegistry registry;
    std::vector<amr::BlockHandle> handles;
    PartitionedTopologyFixture(int dim, int rank, int size, bool periodic, bool refine = true)
        : control(16, dim), registry({dim, {2, 1, 1}, 2}, rank, size) {
        config.grid.dim = dim;
        config.grid.geometry = "cartesian";
        config.grid.nblockx1 = 2;
        config.grid.nblockx2 = dim >= 2 ? 1 : 0;
        config.grid.nblockx3 = dim == 3 ? 1 : 0;
        config.grid.x1_min = config.grid.x2_min = config.grid.x3_min = 0.;
        config.grid.x1_max = config.grid.x2_max = config.grid.x3_max = 1.;
        const auto boundary = periodic ? "periodic" : "outflow";
        config.grid.x1l_boundary_type = config.grid.x1r_boundary_type = boundary;
        config.grid.x2l_boundary_type = config.grid.x2r_boundary_type = boundary;
        config.grid.x3l_boundary_type = config.grid.x3r_boundary_type = boundary;
        config.amr.lrefinemin = config.amr.lrefinemax = 0;
        control.tree->SetRootPartition(rank, size);
        control.tree->InitRootGrid(config, 2);
        const auto observations = [&](std::span<const int> ids) {
            std::vector<TopologyObservation> local;
            for (int id : ids) {
                const auto& b = control.pool->GetBlock(id);
                local.push_back(
                    {id, {dim, b.level, b.logical_x1, b.logical_x2, b.logical_x3}, rank});
            }
            // A serial reference inside an MPI process must not call WORLD.
            return size == 1 ? local : collect_topology_observations(local, rank, size);
        };
        auto initial = registry.stage_adoption(observations(control.tree->GetActiveBlocks()));
        handles = initial.reconciliation().handles_in_observation_order;
        registry.commit(std::move(initial));
        for (int id : control.tree->GetActiveBlocks())
            amr::test::seed_regrid_parent(control.pool->GetBlock(id));
        config.amr.lrefinemax = 1;
        auto prepared = control.tree->PrepareRegrid(config, {}, {}, [&] {
            for (int id : control.tree->GetActiveBlocks()) {
                auto& b = control.pool->GetBlock(id);
                b.refine_flag = refine && b.logical_x1 == 1 ? 1 : 0;
            }
        });
        auto candidate =
            registry.stage_reconciliation(observations(prepared.proposed_active_blocks()));
        const auto next = candidate.reconciliation().handles_in_observation_order;
        if (prepared.topology_changed()) {
            prepared.BuildMigrationPlans(handles, next,
                                         {1, registry.epoch(), candidate.reconciliation().epoch});
            prepared.ExecuteMigration();
            prepared.ActivateForFinalization();
            prepared.PublishNoexcept();
            prepared.ReleaseRetired();
        } else
            prepared.PublishNoChangeNoexcept();
        registry.commit(std::move(candidate));
        handles = next;
        control.tree->UpdateNeighbors(config, registry.blocks());
        control.BindActiveHandles(handles);
        if (control.pool->GetNumActiveBlocks() != static_cast<int>(handles.size()))
            throw std::runtime_error("nonlocal fluid storage exists");
    }
};

} // namespace amr::test
