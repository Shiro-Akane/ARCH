/**
 * @file TimeIntegratorEuler.h
 * @brief 1st Order Forward Euler Time Integrator.
 *
 * Host block execution binds the shared driver/schedule/StageScheduler.h descriptors
 * to concrete state slots. The scheduler owns stage order and weights; the
 * hydro interface performs patch updates, and callbacks synchronize halos,
 * rotate storage, and apply reflux at the prescribed completion boundary.
 * An explicitly bound post-boundary gate also synchronizes the rotated,
 * refluxed Current slot; ordinary stages retain the scheduler's refresh policy.
 */
#pragma once

#include <algorithm>
#include <exception>
#include <string>
#include <vector>

#include "numerics/integrator/IHydroSolver.h"
#include "numerics/integrator/HydroGeometryBinding.h"
#include "numerics/integrator/TimeIntegratorHelper.h"

#include "amr/AMRControl.h"
#include "data/FluidState.h"
#include "driver/schedule/StageScheduler.h"

struct SolverEuler
{
    static std::string name() { return "Euler (1st Order)"; }

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
        const StageBinding& binding = current_stage_binding();
        if (binding.handles.size() != active_blocks.size())
            throw std::logic_error("Euler scheduler handle count mismatch");
        const auto state_for = [](amr::Block& block,
                                  arch::state::StateSlot slot) -> FluidState& {
            switch (slot) {
            case arch::state::StateSlot::Current: return block.fluid_state;
            case arch::state::StateSlot::Next: return block.state_next;
            case arch::state::StateSlot::Scratch: return block.state_scratch;
            }
            throw std::logic_error("Euler descriptor selected unknown slot");
        };

        (void)execute_euler_lane(
            binding.context, binding.handles,
            [&](const StageDescriptor& descriptor,
                arch::state::CompletionToken token) {
                std::exception_ptr stage_failure;
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
                        hydro->evaluate_patch(&amr_ctrl, active_blocks[i], input_state, b.grid, dt, dU, d_spec, gravity, num_cfg, descriptor.flux_register_weight, nullptr);
                        hydro->update_patch(old_state, input_state, output_state, dU, d_spec, b.grid, descriptor.old_weight, descriptor.update_weight, num_cfg, nullptr);
                        } catch (...) {
#pragma omp critical(arch_hydro_failure)
                            { if (!stage_failure) stage_failure = std::current_exception(); }
                        }
                    }
                }
                if (stage_failure) std::rethrow_exception(stage_failure);
                return token;
            },
            [&](arch::state::StateSlot output, arch::state::StateVersion,
                arch::state::CompletionToken token) {
                if (output == arch::state::StateSlot::Scratch)
                    throw std::logic_error(
                        "Euler ghost exchange does not use Scratch output");
                // Current names the actual post-reflux slot. Resolve before
                // either write so invalid slots cannot partially apply BC.
                const auto output_member =
                    TimeIntegration::hydro_boundary_state_member(output);
                TimeIntegration::synchronize_domain_boundary(amr_ctrl, boundary_condition,
                    output_member, binding.handles, geometry.semantics,
                    {num_cfg.sml_rho,num_cfg.min_eint,num_cfg.max_eint});
                return token;
            },
            [&](arch::state::SlotRotation rotation) {
                if (rotation.current_from
                        != arch::state::StateSlot::Next
                    || rotation.next_from
                           != arch::state::StateSlot::Current
                    || rotation.scratch_from
                           != arch::state::StateSlot::Scratch) {
                    throw std::logic_error(
                        "Euler physical rotation descriptor mismatch");
                }
#pragma omp parallel for schedule(dynamic)
                for (size_t i = 0; i < active_blocks.size(); ++i) {
                    amr::Block &b = amr_ctrl.pool->GetBlock(active_blocks[i]);
                    std::swap(b.fluid_state, b.state_next);
                }
            },
            [&](const HydroPlan&, arch::state::StateSlot,
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
