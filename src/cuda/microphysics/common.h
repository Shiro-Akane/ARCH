/**
 * @file common.h
 * @brief Plain burn-policy arguments shared by host and CUDA compile-time calls.
 */
#pragma once

#include "data/FluidState.h"
#include "driver/DriverBurnPolicy.h"
#include "numerics/burnsolver/OdeContinuation.h"
#include "numerics/linalg/DenseWrap.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <utility>

namespace arch::cuda
{
template <int N>
using BurnOdeMatrixWorkspaceFor =
    OdeMatrixWorkspace<DenseMatrixData<N>>;

// Maximum-size workspace alias for focused policy tests. Production
// storage is allocated with BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>.
using BurnOdeMatrixWorkspace =
    BurnOdeMatrixWorkspaceFor<BurnLimits::MAX_ODE_NEQ>;

// Host allocation needs the matrix ABI, not every network's reaction body.
// Count the actual ODE equations, including any integrated source term.
template <std::size_t... Index>
constexpr auto burn_workspace_size_table(std::index_sequence<Index...>)
{
    return std::array<std::size_t, sizeof...(Index)>{
        sizeof(BurnOdeMatrixWorkspaceFor<static_cast<int>(Index + 1)>)...};
}

inline std::size_t compact_burn_workspace_bytes_per_cell(std::size_t equations)
{
    if (!BurnLimits::uses_compact_matrix(equations)) return 0;
    static constexpr auto sizes = burn_workspace_size_table(
        std::make_index_sequence<BurnLimits::MAX_ODE_NEQ>{});
    return sizes[equations - 1];
}

template <int N>
ARCH_INLINE bool burn_ode_workspace_preflight(
    const BurnOdeMatrixWorkspaceFor<N>* workspaces,
    std::size_t workspace_count, std::size_t required_count)
{
    if (required_count == 0) return true;
    return workspaces != nullptr
        && workspace_count >= required_count
        && reinterpret_cast<std::uintptr_t>(workspaces)
               % alignof(BurnOdeMatrixWorkspaceFor<N>) == 0;
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
