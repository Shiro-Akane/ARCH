/**
 * @file AMRControl.h
 * @brief Bundles all AMR-related managers and data structures.
 */

/**
 * Workflow:
 * 1. Build or query topology using the single hierarchy and memory-pool ownership model.
 * 2. Synchronize state or face data with the documented 2:1 AMR index convention.
 * 3. Return conservative leaf data to the driver for refluxing, regridding, or timestep work.
 */

#pragma once

#include <memory>
#include <span>

#include "AmrTree.h"
#include "FluxRegister.h"
#include "GhostExchange.h"
#include "MemoryPool.h"

#include "../grid/GridMetrics.h"

namespace amr {

/**
 * @brief Central controller for AMR, packing the MemoryPool, AmrTree,
 *        GhostExchange, and FluxRegister into a single manageable struct.
 */
struct AMRControl {
    std::shared_ptr<MemoryPool> pool;
    std::shared_ptr<AmrTree> tree;
    GhostExchange ghost_exchange;
    FluxRegister flux_register;

    void BindActiveHandles(std::span<const BlockHandle> handles)
    {
        if (handles.empty())
            throw std::invalid_argument(
                "AMR active-handle binding cannot be empty");
        active_handles_ = handles;
    }

    std::span<const BlockHandle> ActiveHandles() const
    {
        if (active_handles_.empty())
            throw std::logic_error("AMR active handles are not bound");
        return active_handles_;
    }


    void ApplyReflux(double dt, FluidState Block::* state_ptr = &Block::fluid_state) {
        const auto& active_blocks = tree->GetActiveBlocks();
        const auto plan = flux_register.BuildRefluxPlan(
            pool, active_blocks, ActiveHandles(), tree->GetRootGridDim(), dt);
        flux_register.ExecuteRefluxPlan(
            plan, pool, active_blocks, ActiveHandles(), state_ptr);
    }

    AMRControl(int max_blocks, int dim) {
        pool = std::make_shared<MemoryPool>(max_blocks, dim);
        tree = std::make_shared<AmrTree>(pool);
        flux_register.Resize(max_blocks, dim);
    }

private:
    std::span<const BlockHandle> active_handles_{};
};

} // namespace amr
