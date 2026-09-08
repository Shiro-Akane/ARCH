/**
 * @file CudaBackendBurnImpl.cuh
 * @brief Burn kernels and ODE routing shared by narrowly instantiated TUs.
 */

#pragma once

#include "cuda/runtime/burn/CudaBackendBurn.h"
#include "cuda/runtime/burn/CudaBackendBurnReduction.cuh"
#include "cuda/runtime/burn/CudaBurnOdeTypes.h"

#include "cuda/microphysics/microphysics_api.h"
#include "cuda/common/DeviceEosStatus.h"
#include "cuda/microphysics/device_network_owner.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_ros4.h"

#include <type_traits>

namespace arch::cuda::burn_detail {


template <class Network, class OdeBinding, class Eos>
__global__ void burn_cells_kernel(
    DeviceStateView state, DeviceGridView grid,
    BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>* workspaces,
    reduction::ReductionCandidate* candidates, int* statuses,
    double burn_dt, Eos eos, BurnConfigView config, Network network)
{
    const int linear = blockIdx.x * blockDim.x + threadIdx.x;
    const int count = grid.active_cell_count();
    if (linear >= count) return;
    const int nx = grid.ie - grid.is;
    const int ny = grid.je - grid.js;
    const int i = grid.is + linear % nx;
    const int j = grid.js + (linear / nx) % ny;
    const int k = grid.ks + linear / (nx * ny);
    const int cell_index = grid.index(i, j, k);

    BurnPolicyCell cell{};
    cell.fluid = state.load(cell_index);
    for (int species = 0; species < Network::NUM_SPECIES; ++species)
        cell.state[species] = state.species(species, cell_index);
    cell.burn_dt = burn_dt;
    using Ode = OdeType<OdeBinding>;
    constexpr bool shared_workspace =
        std::is_same_v<OdeBinding, dispatch::CudaBeNrBinding>;
    // The shared table EOS latches at its original failure boundary. A later
    // finite temperature/floor must not turn that failed query into a commit.
    // This address is global, not a thread-local atomic destination; each cell
    // owns it until we replace the latch with the final disposition below.
    statuses[linear] = 0;
    const auto checked_eos = bind_device_eos_status(eos, statuses + linear);
    execute_burn_policy_cell<Network, Ode::template solver>(
        cell, workspaces[shared_workspace ? 0 : linear], checked_eos, config, network);
    if (statuses[linear] != 0) {
        cell.ode.status = BurnOdeStatus::EosFailure;
        cell.disposition = DriverBurn::BurnCellDisposition::SolverFailed;
        cell.interior_effect.interior_written = false;
        cell.limiter_candidate = DriverBurn::INACTIVE_LIMITER_CANDIDATE;
    }

    if (cell.interior_effect.interior_written) {
        state.store(cell_index, cell.fluid);
        for (int species = 0; species < Network::NUM_SPECIES; ++species)
            state.mass_fractions[
                static_cast<std::size_t>(species) * state.total_size
                + cell_index] = cell.state[species];
        state.enuc_rate[cell_index] = cell.enuc_rate;
    }
    amr::CellLogicalKey key{};
    key.logical_i = i;
    key.logical_j = j;
    key.logical_k = k;
    key.component = DriverBurn::BURN_LIMITER_COMPONENT;
    candidates[linear] = {cell.limiter_candidate, key, true};
    statuses[linear] = static_cast<int>(cell.disposition);
}


template <class Network, class OdeBinding, class Eos>
cudaError_t launch_route(
    DeviceStateView state, DeviceGridView grid,
    BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>* workspaces,
    reduction::ReductionCandidate* candidates, int* statuses,
    DeviceBurnSummary* summary, double burn_dt, Eos eos,
    BurnConfigView config, cudaStream_t stream, Network network)
{
    OdeMath::check_burn_state_layout<Network>();
    static_assert(BurnLimits::uses_compact_matrix(Network::ODE_NEQ),
                  "Compact burn storage is bounded by the full ODE extent");
    if (state.n_species != Network::NUM_SPECIES)
        return cudaErrorInvalidValue;
    const int count = grid.active_cell_count();
    constexpr int threads = 128;
    const int blocks = (count + threads - 1) / threads;
    burn_cells_kernel<Network, OdeBinding>
        <<<blocks, threads, 0, stream>>>(
            state, grid, workspaces, candidates, statuses, burn_dt, eos,
            config, network);
    cudaError_t error = cudaGetLastError();
    if (error == cudaSuccess) {
        reduce_burn_kernel<Eos><<<1, 1, 0, stream>>>(
            candidates, statuses, count, summary);
        error = cudaGetLastError();
    }
    return error;
}

template <class Eos>
struct RouteContext {
    const dispatch::ResolvedExecutionPlan& plan;
    DeviceStateView state;
    DeviceGridView grid;
    std::byte* workspace_storage;
    reduction::ReductionCandidate* candidates;
    int* statuses;
    DeviceBurnSummary* summary;
    double burn_dt;
    Eos eos;
    CudaBurnArguments config;
    cudaStream_t stream;
    cudaError_t result = cudaErrorInvalidValue;
    bool invoked = false;
};

template <class Network, class Eos>
struct OdeRouteVisitor {
    RouteContext<Eos>& context;

    template <class OdeRegistration>
    void operator()()
    {
        using OdeBinding = typename dispatch::PolicyRegistration<
            OdeRegistration>::CudaBinding;
        if constexpr (BurnLimits::uses_compact_matrix(Network::ODE_NEQ)
                      && !std::is_same_v<OdeBinding, dispatch::AbsentBinding>
                      && !std::is_same_v<
                          OdeBinding, dispatch::CudaNoOdeBinding>) {
            using Workspace = BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>;
            auto* workspaces = reinterpret_cast<Workspace*>(
                context.workspace_storage);
            Network network{};
            if constexpr (requires { Network::host_table_storage(); }) {
                auto* slot = context.config.network_owner;
                if (!slot) return; // A weak route may not borrow a Host table on device.
                const auto host = Network::host_table_storage();
                if (!*slot) *slot = std::make_unique<DeviceNetworkOwner>(host, context.stream);
                if (!(*slot)->bound_to(host, context.stream)) return;
                network = Network{(*slot)->view()};
                if (!network.valid()) return;
            }
            context.result = launch_route<Network, OdeBinding>(
                context.state, context.grid, workspaces,
                context.candidates, context.statuses, context.summary,
                context.burn_dt, context.eos, context.config.controls,
                context.stream, network);
            context.invoked = true;
        }
    }
};

template <class Eos>
bool valid_route_arguments(
    const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state,
    DeviceGridView grid, std::byte* workspace_storage,
    reduction::ReductionCandidate* candidates, int* statuses,
    DeviceBurnSummary* summary, double burn_dt, Eos eos,
    CudaBurnArguments config, cudaStream_t stream)
{
    (void)state;
    (void)grid;
    (void)burn_dt;
    (void)eos;
    (void)config;
    (void)stream;
    return plan.linear_solver == dispatch::LinearSolverId::DenseLu
        && workspace_storage != nullptr && candidates != nullptr
        && statuses != nullptr && summary != nullptr;
}

/**
 * Instantiate exactly one reaction network and the registered CUDA ODE set.
 *
 * Keeping network selection outside this template is intentional: a Tabular
 * dispatcher can live in a lightweight TU while each network owns one bounded
 * NVCC template graph.  The mathematical kernels remain defined once above.
 */
template <class Network, class Eos>
cudaError_t visit_ode_route(
    const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state,
    DeviceGridView grid, std::byte* workspace_storage,
    reduction::ReductionCandidate* candidates, int* statuses,
    DeviceBurnSummary* summary, double burn_dt, Eos eos,
    CudaBurnArguments config, cudaStream_t stream)
{
    if (!valid_route_arguments(
            plan, state, grid, workspace_storage, candidates, statuses,
            summary, burn_dt, eos, config, stream))
        return cudaErrorInvalidValue;
    RouteContext<Eos> context{
        plan, state, grid, workspace_storage, candidates, statuses, summary,
        burn_dt, eos, config, stream};
    OdeRouteVisitor<Network, Eos> visitor{context};
    const bool ode_found = dispatch::visit_policy<
        dispatch::OdeSolverPolicies>(plan.ode_solver, visitor);
    return ode_found && context.invoked
        ? context.result : cudaErrorInvalidValue;
}

} // namespace arch::cuda::burn_detail
