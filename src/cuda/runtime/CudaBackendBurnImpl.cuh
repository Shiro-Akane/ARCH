/**
 * @file CudaBackendBurnImpl.cuh
 * @brief Burn kernels and ODE routing shared by narrowly instantiated TUs.
 */

#pragma once

#include "CudaBackendBurn.h"

#include "cuda/microphysics/microphysics_api.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_ros4.h"

#include <type_traits>

namespace arch::cuda::burn_detail {

template <class Binding>
struct OdeType;
template <>
struct OdeType<dispatch::CudaBeNrBinding> {
    template <class Network, class Matrix, class Linear>
    using solver = Solver_BE_NR<Network, Matrix, Linear>;
};
template <>
struct OdeType<dispatch::CudaBdBinding> {
    template <class Network, class Matrix, class Linear>
    using solver = Solver_BD<Network, Matrix, Linear>;
};
template <>
struct OdeType<dispatch::CudaRos4Binding> {
    template <class Network, class Matrix, class Linear>
    using solver = Solver_ROS4<Network, Matrix, Linear>;
};

template <class Network, class OdeBinding, class Eos>
__global__ void burn_cells_kernel(
    DeviceStateView state, DeviceGridView grid,
    BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>* workspaces,
    reduction::ReductionCandidate* candidates, int* statuses,
    double burn_dt, Eos eos, BurnConfigView config)
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
    execute_burn_policy_cell<Network, Ode::template solver>(
        cell, workspaces[shared_workspace ? 0 : linear], eos, config);

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
    key.component = 3;
    candidates[linear] = {cell.limiter_candidate, key, true};
    statuses[linear] = static_cast<int>(cell.disposition);
}

template <class EosTag>
__global__ void reduce_burn_kernel(
    const reduction::ReductionCandidate* candidates, const int* statuses,
    int count, DeviceBurnSummary* result)
{
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    const auto spec = reduction::minimum_spec(
        DriverBurn::INACTIVE_LIMITER_CANDIDATE);
    auto reduced = reduction::begin_reduction(spec);
    amr::CellLogicalKey seed_key{};
    seed_key.logical_i = -1;
    seed_key.component = 3;
    reduction::combine_candidate(
        spec, reduced,
        {DriverBurn::INACTIVE_LIMITER_CANDIDATE, seed_key, true});
    DeviceBurnSummary summary{};
    for (int cell = 0; cell < count; ++cell) {
        reduction::combine_candidate(spec, reduced, candidates[cell]);
        const auto disposition = static_cast<DriverBurn::BurnCellDisposition>(
            statuses[cell]);
        if (disposition == DriverBurn::BurnCellDisposition::InvalidComposition
            || disposition == DriverBurn::BurnCellDisposition::SolverFailed) {
            ++summary.failed_cells;
            if (summary.status == 0) summary.status = statuses[cell];
        }
    }
    const auto finalized = reduction::finalize_reduction(spec, reduced);
    if (finalized.status != reduction::ReductionStatus::Ok
        && summary.status == 0)
        summary.status = -static_cast<int>(finalized.status) - 1;
    summary.limiter = finalized.value;
    *result = summary;
}

template <class Network, class OdeBinding, class Eos>
cudaError_t launch_route(
    DeviceStateView state, DeviceGridView grid,
    BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>* workspaces,
    reduction::ReductionCandidate* candidates, int* statuses,
    DeviceBurnSummary* summary, double burn_dt, Eos eos,
    BurnConfigView config, cudaStream_t stream)
{
    if (state.n_species != Network::NUM_SPECIES)
        return cudaErrorInvalidValue;
    const int count = grid.active_cell_count();
    constexpr int threads = 128;
    const int blocks = (count + threads - 1) / threads;
    burn_cells_kernel<Network, OdeBinding>
        <<<blocks, threads, 0, stream>>>(
            state, grid, workspaces, candidates, statuses, burn_dt, eos,
            config);
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
    BurnConfigView config;
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
        if constexpr (!std::is_same_v<OdeBinding, dispatch::AbsentBinding>
                      && !std::is_same_v<
                          OdeBinding, dispatch::CudaNoOdeBinding>) {
            using Workspace = BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>;
            auto* workspaces = reinterpret_cast<Workspace*>(
                context.workspace_storage);
            context.result = launch_route<Network, OdeBinding>(
                context.state, context.grid, workspaces,
                context.candidates, context.statuses, context.summary,
                context.burn_dt, context.eos, context.config,
                context.stream);
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
    BurnConfigView config, cudaStream_t stream)
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
    BurnConfigView config, cudaStream_t stream)
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
