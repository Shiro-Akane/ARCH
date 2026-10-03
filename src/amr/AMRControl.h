/**
 * @file AMRControl.h
 * @brief Binds Host AMR storage, hierarchy, exchange, and reflux services.
 *
 * Active block handles connect the logical hierarchy to execution storage.
 * This controller publishes those bindings and caches the corresponding flux
 * plans; scalar transfer and reflux mathematics live in the shared AMR leaves.
 */

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <span>

#include "amr/flux/AmrFluxPlan.h"
#include "amr/topology/AmrTree.h"
#include "amr/flux/FluxRegister.h"
#include "amr/exchange/GhostExchange.h"
#include "amr/storage/MemoryPool.h"

#include "grid/GridMetrics.h"

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
        PublishActiveHandlesNoexcept(handles);
    }

    /**
     * Publish a handle span which has already been validated and whose owner
     * outlives this controller.  Dynamic topology transactions use this only
     * in their no-throw publication tail.
     */
    void PublishActiveHandlesNoexcept(
        std::span<const BlockHandle> handles) noexcept
    {
        active_handles_ = handles;
        std::atomic_store_explicit(
            &reflux_topology_plan_, std::shared_ptr<const CachedRefluxPlan>{},
            std::memory_order_release);
        std::atomic_store_explicit(
            &flux_topology_plan_,
            std::shared_ptr<const AmrFluxTopologyPlan>{},
            std::memory_order_release);
    }

    std::span<const BlockHandle> ActiveHandles() const
    {
        if (active_handles_.empty())
            throw std::logic_error("AMR active handles are not bound");
        return active_handles_;
    }

    const AmrFluxTopologyPlan& RequireFluxTopologyPlan(int species_count,
        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing,
        int source_block = -1)
    {
        const auto handles = ActiveHandles();
        const int dimension = tree->GetRootGridDim();
        auto cached = std::atomic_load_explicit(
            &flux_topology_plan_, std::memory_order_acquire);
        if (cached && cached->species_count == species_count
            && cached->dimension == dimension
            && cached->epoch == handles.front().epoch
            && matches_amr_flux_geometry(*cached,*pool,tree->GetActiveBlocks(),semantics,source_block))
            return *cached;

        std::lock_guard lock(flux_plan_mutex_);
        cached = std::atomic_load_explicit(
            &flux_topology_plan_, std::memory_order_acquire);
        if (!cached || cached->species_count != species_count
            || cached->dimension != dimension
            || cached->epoch != handles.front().epoch
            || !matches_amr_flux_geometry(*cached,*pool,tree->GetActiveBlocks(),semantics,source_block)) {
            cached = std::make_shared<const AmrFluxTopologyPlan>(
                build_amr_flux_topology_plan(
                    *pool, tree->GetActiveBlocks(), handles,
                    dimension, species_count,semantics));
            std::atomic_store_explicit(
                &reflux_topology_plan_,
                std::shared_ptr<const CachedRefluxPlan>{},
                std::memory_order_release);
            std::atomic_store_explicit(
                &flux_topology_plan_, cached, std::memory_order_release);
        }
        return *cached;
    }

    const RefluxPlan& RequireRefluxTopologyPlan(int species_count,
        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
    {
        const auto& topology = RequireFluxTopologyPlan(species_count,semantics);
        auto cached = std::atomic_load_explicit(
            &reflux_topology_plan_, std::memory_order_acquire);
        if (cached && cached->topology_fingerprint == topology.fingerprint)
            return cached->plan;
        std::lock_guard lock(flux_plan_mutex_);
        cached = std::atomic_load_explicit(
            &reflux_topology_plan_, std::memory_order_acquire);
        if (!cached || cached->topology_fingerprint != topology.fingerprint) {
            cached = std::make_shared<const CachedRefluxPlan>(
                CachedRefluxPlan{topology.fingerprint,
                    build_amr_reflux_topology_plan(*pool, topology)});
            std::atomic_store_explicit(
                &reflux_topology_plan_, cached, std::memory_order_release);
        }
        return cached->plan;
    }


    void ApplyReflux(double dt, FluidState Block::* state_ptr = &Block::fluid_state,
        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing) {
        const auto& active_blocks = tree->GetActiveBlocks();
        const auto& plan = RequireRefluxTopologyPlan(
            flux_register.GetNumSpecies(),semantics);
        const auto& topology=RequireFluxTopologyPlan(
            flux_register.GetNumSpecies(),semantics);
        flux_register.ValidateTopologyIdentity(topology.fingerprint,
            !plan.operations.empty()
                && semantics==GridMetrics::GeometrySemantics::AxisymmetricRz);
        flux_register.ExecuteRefluxPlan(
            plan, pool, active_blocks, ActiveHandles(), state_ptr, dt);
    }

    AMRControl(int max_blocks, int dim) {
        pool = std::make_shared<MemoryPool>(max_blocks, dim);
        tree = std::make_shared<AmrTree>(pool);
        flux_register.Resize(max_blocks, dim);
    }

private:
    std::span<const BlockHandle> active_handles_{};
    std::shared_ptr<const AmrFluxTopologyPlan> flux_topology_plan_;
    struct CachedRefluxPlan {
        std::uint64_t topology_fingerprint;
        RefluxPlan plan;
    };
    std::shared_ptr<const CachedRefluxPlan> reflux_topology_plan_;
    std::mutex flux_plan_mutex_;
};

} // namespace amr
