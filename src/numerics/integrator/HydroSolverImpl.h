/**
 * @file HydroSolverImpl.h
 * @brief Bridges concrete EOS/flux templates to the block-level hydro interface.
 *
 * This Host adapter allocates patch flux buffers and forwards the configured
 * EOS/flux combination to TimeIntegratorHelper.h. The helper owns divergence
 * and stage-update mathematics; this binding does not define another scheme.
 * Workflow: authenticate physical walls; claim exactly the selected private
 * external/self source frame; allocate existing scratch; run the selected
 * shared flux/EOS traversal and complete that actual patch source receipt.
 */

#pragma once

#include <optional>
#include <stdexcept>
#include <vector>

#include "numerics/integrator/HydroBoundaryAuthority.h"
#include "numerics/integrator/IHydroSolver.h"
#include "numerics/integrator/TimeIntegratorHelper.h"
#include "physics/gravity/NativeSelfStage.h"

namespace Numerics {

/**
 * @brief Concrete implementation of IHydroSolver.
 * Instantiated for a specific combination of EosType and FluxSchemePolicy.
 */
template <typename EosType, typename FluxSchemePolicy>
class HydroSolverImpl : public IHydroSolver {
public:
    explicit HydroSolverImpl(const EosType& eos,
        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
        : eos_(eos), semantics_(semantics)
    {
        if (semantics != GridMetrics::GeometrySemantics::Existing
            && semantics != GridMetrics::GeometrySemantics::AxisymmetricRz)
            throw std::invalid_argument("Unknown Host Hydro geometry semantics");
    }

    GridMetrics::GeometrySemantics geometry_semantics() const noexcept override
    {
        return semantics_;
    }

    /** Shared Host stage updates write fixed extents; accepted integrators rotate live slots. */
    HostHydroStorageContract host_storage_contract() const noexcept override {
        return HostHydroStorageContract::FixedExtentSlotPermutation;
    }

    virtual void evaluate_patch(amr::AMRControl* amr_ctrl, int block_id,
                                const FluidState& state, const Grid& grid, double dt,
                                std::vector<FluidVector>& dU, std::vector<double>& d_spec,
                                const Physical::Gravity::IGravityPolicy* gravity,
                                const NumericsConfig& num_cfg, double flux_weight = 1.0,
                                void* execution_stream = nullptr,
                                const arch::boundary::HostHydroBoundaryAuthority* boundary = nullptr) const override
    {
        // Authenticate the exact borrowed BC/slot/ghost frame before outputs,
        // cache allocation, or EOS work. The empty mathematical path is used
        // by direct numerical leaves; production BCHandler integrators bind the same real input frame.
        if(boundary&&boundary->geometry_semantics()!=semantics_)
            throw std::invalid_argument("Hydro wall authority and solver chart mismatch");
        const auto walls=boundary
            ? boundary->require_view(amr_ctrl,block_id,state,grid)
            : arch::boundary::HydroBoundaryView{};
        std::optional<Physical::Gravity::NativeExternalStageFrame::PatchReceipt> source;
        std::optional<Physical::Gravity::NativeSelfStageFrame::PatchReceipt> self_source;
        if(semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz&&gravity) {
            // Claim exactly one private prepared frame before any output/cache
            // buffer allocation. The public origin enum never grants a claim.
            using Physical::Gravity::GravitySourceOrigin;
            const auto* external=gravity->prepared_native_external();
            const auto* self=gravity->prepared_native_self();
            if(external&&self)throw std::logic_error("Native source has two actual prepared stage frames");
            switch(gravity->source_descriptor().origin) {
            case GravitySourceOrigin::NativeExternalOrthonormal:
                if(!external||self)throw std::logic_error("Native external source has no actual prepared stage frame");
                source.emplace(external->claim_patch(amr_ctrl,block_id,state,grid,dt,*gravity));
                break;
            case GravitySourceOrigin::NativeSelfComposite:
                if(!self||external)throw std::logic_error("Native self source has no matching actual stage frame");
                self_source.emplace(self->claim_patch(amr_ctrl,block_id,state,grid,dt,*gravity));
                break;
            default:
                throw std::logic_error("Native source has an unknown prepared origin");
            }
        }
        int total_size = grid.GetTotalSize();
        int n_spec = state.GetNumSpecies();

        std::vector<FluidVector> flux_buffer(total_size);
        std::vector<double> spec_flux_buffer(n_spec * total_size);

        const auto evaluate = [&] {
            TimeIntegration::evaluate_all_dimensions<FluxSchemePolicy, EosType>(
                amr_ctrl, block_id, state, eos_, grid, dt, dU, d_spec,
                flux_buffer, spec_flux_buffer, gravity, num_cfg.entropy_fix_coeff, flux_weight, num_cfg.hll_roe_wave_speed, semantics_,
                {num_cfg.sml_rho,num_cfg.min_eint,num_cfg.max_eint},walls,source?&*source:nullptr,
                self_source?&*self_source:nullptr);
        };
        if constexpr (requires { typename EosType::HostHydroScope; }) {
            // The EOS owns the complete key. Storage is local to this worker
            // and expires before another patch, RK stage or table can enter.
            typename EosType::HostHydroScope workspace(eos_);
            evaluate();
        } else evaluate();
    }

    virtual void update_patch(const FluidState& state_old, const FluidState& state_curr, FluidState& state_new,
                              const std::vector<FluidVector>& dU, const std::vector<double>& d_spec,
                              const Grid& grid, double w_old, double w_flux,
                              const NumericsConfig& num_cfg,
                              void* execution_stream = nullptr) const override
    {
        TimeIntegration::perform_stage_update(
            state_old, state_curr, state_new, dU, d_spec, grid,
            w_old, w_flux, num_cfg.sml_rho, num_cfg.min_eint,
            num_cfg.max_eint, semantics_);
    }

private:
    const EosType& eos_;
    const GridMetrics::GeometrySemantics semantics_;
};

} // namespace Numerics
