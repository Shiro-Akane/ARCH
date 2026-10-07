/**
 * @file TimeIntegratorRK3.h
 * @brief 3rd Order Strong Stability Preserving Runge-Kutta (SSPRK3) Time Integrator.
 *
 * Host block execution binds the shared driver/schedule/StageScheduler.h descriptors
 * to concrete state slots. The scheduler owns stage order and weights; the
 * hydro interface performs patch updates, and callbacks synchronize halos,
 * rotate storage, and apply reflux at the prescribed completion boundary.
 * An explicitly bound post-boundary gate synchronizes every stage output
 * and the rotated, refluxed Current slot with the same storage for BC/exchange.
 */

#pragma once

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "numerics/integrator/IHydroSolver.h"
#include "numerics/integrator/HydroGeometryBinding.h"
#include "numerics/integrator/HydroBoundaryAuthority.h"
#include "numerics/integrator/TimeIntegratorHelper.h"

#include "amr/AMRControl.h"
#include "data/FluidState.h"
#include "driver/schedule/StageScheduler.h"

struct SolverRK3
{
    static std::string name() { return "SSPRK3"; }

    /**
     * @brief Performs one full RK3 time step.
     */
    template <typename BCPolicy>
    static void solve(amr::AMRControl &amr_ctrl,
                      double dt,
                      BCPolicy &boundary_condition,
                      const Physical::Gravity::IGravityPolicy* gravity,
                      const Numerics::IHydroSolver* hydro,
                      const NumericsConfig &num_cfg)
    {
        const auto geometry=TimeIntegration::bind_hydro_geometry(
            amr_ctrl,boundary_condition,hydro,gravity);
        amr_ctrl.flux_register.Clear();
        const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
        if (!active_blocks.empty()) {
            amr_ctrl.flux_register.EnsureSpecies(amr_ctrl.pool->GetBlock(active_blocks[0]).fluid_state.GetNumSpecies());
        }
        int total_size = active_blocks.empty() ? 0 : amr_ctrl.pool->GetBlock(active_blocks[0]).grid.GetTotalSize();
        int dim = amr_ctrl.tree->GetRootGridDim();

        using namespace arch::scheduler;
        using arch::state::StateSlot;
        const StageBinding& binding = current_stage_binding();
        if (binding.handles.size() != active_blocks.size())
            throw std::logic_error("RK3 scheduler handle count mismatch");
        const auto state_for = [](amr::Block& block,
                                  StateSlot slot) -> FluidState& {
            switch (slot) {
            case StateSlot::Current: return block.fluid_state;
            case StateSlot::Next: return block.state_next;
            case StateSlot::Scratch: return block.state_scratch;
            }
            throw std::logic_error("RK3 descriptor selected unknown slot");
        };

        (void)execute_rk3_lane(
            binding.context, binding.handles,
                [&](const StageDescriptor& descriptor,
                    arch::state::CompletionToken token) {
                std::exception_ptr stage_failure;
                // Source preparation has finished. Capture the whole real
                // synchronous read domain before any OMP output/cache work;
                // this nonmoving metadata owner expires before publication.
                std::optional<arch::boundary::HostHydroBoundaryDomainAuthority> wall_domain;
                if(geometry.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
                    if constexpr(std::is_same_v<std::remove_cvref_t<BCPolicy>,BCHandler>)
                        wall_domain.emplace(boundary_condition,amr_ctrl,binding,descriptor);
                    else throw std::invalid_argument("Native Hydro requires the actual physical boundary authority");
                }
#pragma omp parallel
                    {
                        int n_spec = active_blocks.empty() ? 0 : amr_ctrl.pool->GetBlock(active_blocks[0]).fluid_state.GetNumSpecies();
                        std::vector<FluidVector> dU(total_size);
                        std::vector<double> d_spec(n_spec * total_size);

#pragma omp for schedule(dynamic)
                        for (size_t i = 0; i < active_blocks.size(); ++i) {
                            try {
                        amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);
                            FluidState& old_state = state_for(b, descriptor.old_slot);
                            FluidState& input_state = state_for(b, descriptor.input_slot);
                            FluidState& output_state = state_for(b, descriptor.output_slot);
                            // The main-thread binding is borrowed explicitly by this OMP
                        // worker; wall authority expires with this patch-stage.
                        std::optional<arch::boundary::HostHydroBoundaryAuthority> wall;
                        if(geometry.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
                            if constexpr(std::is_same_v<std::remove_cvref_t<BCPolicy>,BCHandler>)
                                wall.emplace(*wall_domain,i,active_blocks[i],input_state,b.grid);
                            else throw std::invalid_argument("Native Hydro requires the actual physical boundary authority");
                        }
                        hydro->evaluate_patch(&amr_ctrl, active_blocks[i], input_state, b.grid, dt, dU, d_spec, gravity, num_cfg, descriptor.flux_register_weight, nullptr,wall?&*wall:nullptr);
                            hydro->update_patch(old_state, input_state, output_state, dU, d_spec, b.grid, descriptor.old_weight, descriptor.update_weight, num_cfg, nullptr);
                        } catch (...) {
#pragma omp critical(arch_hydro_failure)
                            { if (!stage_failure) stage_failure = std::current_exception(); }
                        }
                        }
                    }
                    // All workers have joined, including on numerical rejection.
                    // Complete-domain identity still gates publication; retain
                    // the original first numerical exception if both fail.
                    try { if(wall_domain)wall_domain->require_complete_domain(); }
                    catch(...) { if(!stage_failure)stage_failure=std::current_exception(); }
                    if (stage_failure) std::rethrow_exception(stage_failure);
                return token;
                },
                [&](StateSlot output, arch::state::StateVersion,
                    arch::state::CompletionToken token) {
                    // Current is the real post-reflux slot. Reject unknown
                    // enums before any block is modified by BC or exchange.
                    const auto output_member =
                        TimeIntegration::hydro_boundary_state_member(output);
                    TimeIntegration::synchronize_domain_boundary(amr_ctrl, boundary_condition,
                        output_member, binding.handles, geometry.semantics,
                        {num_cfg.sml_rho,num_cfg.min_eint,num_cfg.max_eint});
                    return token;
                },
            [&](arch::state::SlotRotation rotation) {
                if (rotation.current_from != StateSlot::Scratch
                    || rotation.next_from != StateSlot::Next
                    || rotation.scratch_from != StateSlot::Current) {
                    throw std::logic_error("RK3 physical rotation descriptor mismatch");
                }
#pragma omp parallel for schedule(dynamic)
                for (size_t i = 0; i < active_blocks.size(); ++i) {
                    amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);
                    std::swap(b.fluid_state, b.state_scratch);
                }
            },
            [&](const HydroPlan&, StateSlot,
                arch::state::CompletionToken token) {
                if (geometry.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz)
                    TimeIntegration::begin_rz_hydro_reflux_receipts(amr_ctrl);
                amr_ctrl.ApplyReflux(dt,&amr::Block::fluid_state,geometry.semantics,
                    geometry.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz);
                if (geometry.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz)
                    TimeIntegration::validate_reflux_state(amr_ctrl,num_cfg);
                else TimeIntegration::accept_reflux_state(amr_ctrl,num_cfg);
                return token;
            });
    }
};
