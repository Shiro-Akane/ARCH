/**
 * @file TimeIntegratorRK3.h
 * @brief 3rd Order Strong Stability Preserving Runge-Kutta (SSPRK3) Time Integrator.
 *
 * Host block execution binds the shared driver/StageScheduler.h descriptors
 * to concrete state slots. The scheduler owns stage order and weights; the
 * hydro interface performs patch updates, and callbacks synchronize halos,
 * rotate storage, and apply reflux at the prescribed completion boundary.
 */

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "IHydroSolver.h"
#include "TimeIntegratorHelper.h"

#include "../../amr/AMRControl.h"
#include "../../data/FluidState.h"
#include "../../driver/StageScheduler.h"

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
#pragma omp parallel
                    {
                        int n_spec = active_blocks.empty() ? 0 : amr_ctrl.pool->GetBlock(active_blocks[0]).fluid_state.GetNumSpecies();
                        std::vector<FluidVector> dU(total_size);
                        std::vector<double> d_spec(n_spec * total_size);

#pragma omp for schedule(dynamic)
                        for (size_t i = 0; i < active_blocks.size(); ++i) {
                            amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);
                            FluidState& old_state = state_for(b, descriptor.old_slot);
                            FluidState& input_state = state_for(b, descriptor.input_slot);
                            FluidState& output_state = state_for(b, descriptor.output_slot);
                            hydro->evaluate_patch(&amr_ctrl, active_blocks[i], input_state, b.grid, dt, dU, d_spec, gravity, num_cfg, descriptor.flux_register_weight, nullptr);
                            hydro->update_patch(old_state, input_state, output_state, dU, d_spec, b.grid, descriptor.old_weight, descriptor.update_weight, num_cfg, nullptr);
                        }
                    }
                    return token;
                },
                [&](StateSlot output, arch::state::StateVersion,
                    arch::state::CompletionToken token) {
#pragma omp parallel for schedule(dynamic)
                    for (size_t i = 0; i < active_blocks.size(); ++i) {
                        amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);
                        boundary_condition.apply(state_for(b, output), b.grid);
                    }
                    FluidState amr::Block::* output_member = nullptr;
                    if (output == StateSlot::Scratch)
                        output_member = &amr::Block::state_scratch;
                    else if (output == StateSlot::Next)
                        output_member = &amr::Block::state_next;
                    else
                        throw std::logic_error("RK3 ghost exchange selected Current output");
                    amr_ctrl.ghost_exchange.ExecuteExchange(
                        amr_ctrl.pool, amr_ctrl.tree, dim, output_member,
                        binding.handles);
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
                amr_ctrl.ApplyReflux(dt);
                return token;
            });
    }
};
