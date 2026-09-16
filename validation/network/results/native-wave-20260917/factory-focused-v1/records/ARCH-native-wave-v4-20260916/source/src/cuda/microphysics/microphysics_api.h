/**
 * @file microphysics_api.h
 * @brief Compile-time calls into the shared DriverBurn and ODE policy leaves.
 */
#pragma once

#include "cuda/microphysics/common.h"

namespace arch::cuda
{
template <typename NetType, template <typename, typename, typename> class Solver,
          typename EosPolicy, int MatrixExtent>
ARCH_INLINE void execute_ode_policy(
    BurnPolicyCell& cell,
    BurnOdeMatrixWorkspaceFor<MatrixExtent>& workspace,
    const EosPolicy& eos, const BurnConfigView& burn_cfg, const NetType& network = {})
{
    static_assert(MatrixExtent >= NetType::ODE_NEQ);
    cell.eint_old = DriverBurn::recover_burn_internal_energy(
        cell.fluid.rho, cell.state[NetType::NUM_SPECIES], cell.state, eos);
    cell.dt_recommended = cell.burn_dt;
    cell.ode = Solver<NetType, DenseMatrixData<MatrixExtent>,
                      DenseLUSolver>::integrate_report(
        cell.state, cell.fluid.rho, cell.burn_dt, eos, burn_cfg,
        workspace, cell.dt_recommended, network);
    cell.ode.dt_recommended = cell.dt_recommended;
    if (cell.ode.success())
        cell.eint_new = DriverBurn::recover_burn_internal_energy(
            cell.fluid.rho, cell.state[NetType::NUM_SPECIES], cell.state, eos);
}

template <typename NetType, template <typename, typename, typename> class Solver,
          typename EosPolicy, int MatrixExtent>
ARCH_INLINE void execute_burn_policy_cell(
    BurnPolicyCell& cell,
    BurnOdeMatrixWorkspaceFor<MatrixExtent>& workspace,
    const EosPolicy& eos, const BurnConfigView& burn_cfg, const NetType& network = {})
{
    static_assert(MatrixExtent >= NetType::ODE_NEQ);
    cell.enuc_rate = 0.0;
    cell.limiter_candidate = DriverBurn::INACTIVE_LIMITER_CANDIDATE;
    cell.interior_effect.interior_written = false;
    if (!burn_cfg.use_burn)
    {
        cell.disposition = DriverBurn::BurnCellDisposition::BurnDisabled;
        return;
    }

    cell.disposition = DriverBurn::check_burn_density(cell.fluid, burn_cfg);
    if (cell.disposition == DriverBurn::BurnCellDisposition::BelowDensity)
        return;

    const auto prepared = DriverBurn::prepare_burn_cell(
        cell.fluid, cell.state, NetType::NUM_SPECIES, eos, burn_cfg);
    cell.disposition = prepared.disposition;
    if (prepared.disposition != DriverBurn::BurnCellDisposition::Ready)
        return;

    const double burn_dt = cell.burn_dt;
    cell.dt_recommended = burn_dt;
    cell.ode = Solver<NetType, DenseMatrixData<MatrixExtent>,
                      DenseLUSolver>::integrate_report(
        cell.state, cell.fluid.rho, burn_dt, eos, burn_cfg,
        workspace, cell.dt_recommended, network);
    cell.ode.dt_recommended = cell.dt_recommended;
    if (!cell.ode.success())
    {
        cell.disposition = DriverBurn::BurnCellDisposition::SolverFailed;
        return;
    }

    const auto handoff = DriverBurn::compute_burn_energy_handoff(
        cell.fluid, cell.state, NetType::NUM_SPECIES,
        prepared.internal_energy, prepared.kinetic_energy,
        burn_dt,
        eos, burn_cfg, cell.ode.energy_change);
    if (!handoff.valid) {
        cell.disposition = DriverBurn::BurnCellDisposition::SolverFailed;
        cell.ode.status = BurnOdeStatus::EosFailure;
        return;
    }
    DriverBurn::commit_burn_energy(cell.fluid, handoff);
    cell.eint_old = prepared.internal_energy;
    cell.eint_new = handoff.new_internal_energy;
    cell.enuc_rate = handoff.enuc_rate;
    cell.limiter_candidate = handoff.limiter_candidate;
    cell.interior_effect.interior_written = true;
}
} // namespace arch::cuda
