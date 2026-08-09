/**
 * @file HydroSolverImpl.h
 * @brief Bridges concrete EOS/flux templates to the block-level hydro interface.
 *
 * Workflow:
 * 1. Evaluate block-local flux divergence and physical source terms.
 * 2. Combine stages with the documented Euler, RK2, or RK3 coefficients.
 * 3. Leave AMR communication and reflux ownership with the common driver services.
 */

#pragma once

#include "IHydroSolver.h"
#include "TimeIntegratorHelper.h"

namespace Numerics {

/**
 * @brief Concrete implementation of IHydroSolver.
 * Instantiated for a specific combination of EosType and FluxSchemePolicy.
 */
template <typename EosType, typename FluxSchemePolicy>
class HydroSolverImpl : public IHydroSolver {
public:
    HydroSolverImpl(const EosType& eos) : eos_(eos) {}

    virtual void evaluate_patch(amr::AMRControl* amr_ctrl, int block_id,
                                const FluidState& state, const Grid& grid, double dt,
                                std::vector<FluidVector>& dU, std::vector<double>& d_spec,
                                const Physical::Gravity::IGravityPolicy* gravity,
                                const NumericsConfig& num_cfg, double flux_weight = 1.0,
                                void* execution_stream = nullptr) const override
    {
        int total_size = grid.GetTotalSize();
        int n_spec = state.GetNumSpecies();

        std::vector<FluidVector> flux_buffer(total_size);
        std::vector<double> spec_flux_buffer(n_spec * total_size);

        TimeIntegration::evaluate_all_dimensions<FluxSchemePolicy, EosType>(
            amr_ctrl, block_id, state, eos_, grid, dt, dU, d_spec, flux_buffer, spec_flux_buffer, gravity, num_cfg.entropy_fix_coeff, flux_weight);
    }

    virtual void update_patch(const FluidState& state_old, const FluidState& state_curr, FluidState& state_new,
                              const std::vector<FluidVector>& dU, const std::vector<double>& d_spec,
                              const Grid& grid, double w_old, double w_flux,
                              const NumericsConfig& num_cfg,
                              void* execution_stream = nullptr) const override
    {
        TimeIntegration::perform_stage_update(state_old, state_curr, state_new, dU, d_spec, grid, w_old, w_flux, num_cfg.sml_rho, num_cfg.max_eint);
    }

private:
    const EosType& eos_;
};

} // namespace Numerics
