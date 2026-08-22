/**
 * @file common.h
 * @brief Plain burn-policy arguments shared by host and CUDA compile-time calls.
 */
#pragma once

#include "data/FluidState.h"
#include "driver/DriverBurn.h"
#include "numerics/burnsolver/odeFunction.h"
#include "numerics/linalg/DenseWrap.h"

#include <cstddef>
#include <cstdint>

namespace arch::cuda
{
using BurnOdeMatrixWorkspace = OdeMatrixWorkspace<DenseMatrixData>;

ARCH_INLINE bool burn_ode_workspace_preflight(
    const BurnOdeMatrixWorkspace* workspaces,
    std::size_t workspace_count, std::size_t required_count)
{
    if (required_count == 0) return true;
    return workspaces != nullptr
        && workspace_count >= required_count
        && reinterpret_cast<std::uintptr_t>(workspaces)
               % alignof(BurnOdeMatrixWorkspace) == 0;
}

struct BurnInteriorEffect
{
    bool interior_written = false;
};

struct BurnPolicyCell
{
    FluidVector fluid{};
    double state[BurnLimits::MAX_ODE_NEQ]{};
    double burn_dt = 0.0;
    double dt_recommended = 0.0;
    double eint_old = 0.0;
    double eint_new = 0.0;
    BurnOdeReport ode{};
    DriverBurn::BurnCellDisposition disposition =
        DriverBurn::BurnCellDisposition::BurnDisabled;
    BurnInteriorEffect interior_effect{};
    double enuc_rate = 0.0;
    double limiter_candidate = DriverBurn::INACTIVE_LIMITER_CANDIDATE;
};
} // namespace arch::cuda
