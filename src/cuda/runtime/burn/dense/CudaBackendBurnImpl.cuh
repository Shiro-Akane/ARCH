/**
 * @file CudaBackendBurnImpl.cuh
 * @brief Dense burn kernels and shared ODE routing for typed CUDA instantiations.
 *
 * Cell preparation and thermodynamic commit use DriverBurnPolicy around the
 * selected shared ODE policy. The runtime supplies persistent workspaces and
 * owns stream completion; a common reduction reports cell failures and limits.
 * Workflow:
 * 1. Receive a selected dense burn route and device state.
 * 2. Bind the compact network/ODE work to CUDA launch storage.
 * 3. Return results through the shared burn-policy report.
 */

#pragma once

#include "cuda/runtime/burn/CudaBackendBurn.h"
#include "cuda/runtime/burn/CudaBackendBurnReduction.cuh"
#include "cuda/runtime/burn/CudaBurnOdeTypes.h"

#include "cuda/microphysics/microphysics_api.h"
#include "cuda/microphysics/burn/NativeBurnState.cuh"
#include "cuda/common/DeviceEosStatus.h"
#include "cuda/common/ExactWarpGroup.cuh"
#include "cuda/microphysics/network/device_network_owner.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "numerics/burnsolver/ode/ode_bd.h"
#include "numerics/burnsolver/ode/ode_be-nr.h"
#include "numerics/burnsolver/ode/ode_ros4.h"

#include <type_traits>
#include <algorithm>

namespace arch::cuda::burn_detail {


template <class Network, class OdeBinding, class Eos>
__global__ void burn_cells_kernel(
    DeviceStateView state, DeviceGridView grid,
    BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>* workspaces,
    reduction::ReductionCandidate* candidates, int* statuses,
    double burn_dt, Eos eos, BurnConfigView config, Network network,
    const DeviceBurnBatchBlock* blocks = nullptr)
{
    if (blocks) {
        const auto& block = blocks[blockIdx.y];
        state = block.state;
        grid = block.grid;
        workspaces = reinterpret_cast<BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>*>(
            block.workspace_storage);
        candidates = block.candidates;
        statuses = block.statuses;
    }
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
    const bool native = grid.semantics == GridMetrics::GeometrySemantics::AxisymmetricRz;
    bool native_input_valid = true;
    if (native && config.use_burn)
        native_input_valid = blocks && prepare_native_burn_mean(
            state, grid, cell_index, i, blocks[blockIdx.y].bounds, cell.fluid)
            == arch::state::Status::valid;
    using Ode = OdeType<OdeBinding>;
    constexpr bool shared_workspace =
        std::is_same_v<OdeBinding, dispatch::CudaBeNrBinding>;
    // The shared table EOS latches at its original failure boundary. A later
    // finite temperature/floor must not turn that failed query into a commit.
    // This address is global, not a thread-local atomic destination; each cell
    // owns it until the final disposition replaces the latch below.
    statuses[linear] = 0;
    const auto checked_eos = bind_device_eos_status(eos, statuses + linear);
    // A kernel fixes the network, EOS tables, controls and interval. Include
    // every remaining physical input before delegating once to the same policy.
    ExactWarpGroup group;
    group.match(static_cast<int>(native_input_valid));
    group.match(cell.fluid.rho); group.match(cell.fluid.mom_u);
    group.match(cell.fluid.mom_v); group.match(cell.fluid.mom_w);
    group.match(cell.fluid.eng); group.match(cell.burn_dt);
    for (int species = 0; species < Network::NUM_SPECIES; ++species)
        group.match(cell.state[species]);
    if (group.leader()) {
        if (native_input_valid)
            execute_burn_policy_cell<Network, Ode::template solver>(
                cell, workspaces[shared_workspace ? 0 : linear], checked_eos, config, network);
        else {
            cell.ode.status = BurnOdeStatus::EosFailure;
            cell.disposition = DriverBurn::BurnCellDisposition::SolverFailed;
        }
    }
    // Preserve the original failure latch before each lane publishes its own
    // disposition. Never let a finite fallback erase the leader's EOS failure.
    const int eos_failed = group.broadcast(statuses[linear]);
    cell.fluid.rho = group.broadcast(cell.fluid.rho);
    cell.fluid.mom_u = group.broadcast(cell.fluid.mom_u);
    cell.fluid.mom_v = group.broadcast(cell.fluid.mom_v);
    cell.fluid.mom_w = group.broadcast(cell.fluid.mom_w);
    cell.fluid.eng = group.broadcast(cell.fluid.eng);
    for (int component = 0; component < Network::ODE_NEQ; ++component)
        cell.state[component] = group.broadcast(cell.state[component]);
    cell.dt_recommended = group.broadcast(cell.dt_recommended);
    cell.eint_old = group.broadcast(cell.eint_old); cell.eint_new = group.broadcast(cell.eint_new);
    cell.enuc_rate = group.broadcast(cell.enuc_rate);
    cell.limiter_candidate = group.broadcast(cell.limiter_candidate);
    cell.ode.status = static_cast<BurnOdeStatus>(group.broadcast(static_cast<int>(cell.ode.status)));
    cell.ode.attempted_substeps = group.broadcast(cell.ode.attempted_substeps);
    cell.ode.rejected_substeps = group.broadcast(cell.ode.rejected_substeps);
    cell.ode.nse_attempts = group.broadcast(cell.ode.nse_attempts);
    cell.ode.nse_failures = group.broadcast(cell.ode.nse_failures);
    cell.ode.dt_recommended = group.broadcast(cell.ode.dt_recommended);
    cell.ode.energy_change = group.broadcast(cell.ode.energy_change);
    cell.disposition = static_cast<DriverBurn::BurnCellDisposition>(
        group.broadcast(static_cast<int>(cell.disposition)));
    cell.interior_effect.interior_written = group.broadcast(
        static_cast<int>(cell.interior_effect.interior_written)) != 0;
    bool invalid_state = false;
    if (blocks && cell.interior_effect.interior_written) {
        const auto bounds = blocks[blockIdx.y].bounds;
        invalid_state = arch::state::validate(cell.fluid, cell.state, Network::NUM_SPECIES, 1,
            bounds.density, bounds.internal_min, bounds.internal_max) != arch::state::Status::valid;
    }
    if (eos_failed != 0 || invalid_state) {
        cell.ode.status = BurnOdeStatus::EosFailure;
        cell.disposition = DriverBurn::BurnCellDisposition::SolverFailed;
        cell.interior_effect.interior_written = false;
        cell.limiter_candidate = DriverBurn::INACTIVE_LIMITER_CANDIDATE;
    }

    if (cell.interior_effect.interior_written) {
        // Grouped ordinary effective states never replace a lane's native J/W.
        if (native) state.eng[cell_index] = cell.fluid.eng;
        else state.store(cell_index, cell.fluid);
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
    CudaBurnArguments config, cudaStream_t stream, Network network)
{
    OdeMath::check_burn_state_layout<Network>();
    static_assert(BurnLimits::uses_compact_matrix(Network::ODE_NEQ),
                  "Compact burn storage is bounded by the full ODE extent");
    if (state.n_species != Network::NUM_SPECIES)
        return cudaErrorInvalidValue;
    if (config.host_blocks.empty() != (config.device_blocks == nullptr))
        return cudaErrorInvalidValue;
    // Native candidates require the configured bounds in an owned block record.
    if (config.controls.use_burn && config.host_blocks.empty()
        && grid.semantics == GridMetrics::GeometrySemantics::AxisymmetricRz)
        return cudaErrorInvalidValue;
    // Validate every borrowed record before any wave writes. The backend has
    // already checked unique handles, generations and Current-slot ownership.
    for (const auto& block : config.host_blocks)
        if (block.state.n_species != Network::NUM_SPECIES
            || block.grid.active_cell_count() <= 0 || !block.workspace_storage
            || !block.candidates || !block.statuses || !block.summary)
            return cudaErrorInvalidValue;
    if (!config.host_blocks.empty()) {
        for (std::size_t first = 0; first < config.host_blocks.size(); first += BURN_BATCH_WAVE_LIMIT) {
            const auto count = std::min(BURN_BATCH_WAVE_LIMIT, config.host_blocks.size() - first);
            int cells = 0;
            for (const auto& block : config.host_blocks.subspan(first, count))
                cells = std::max(cells, block.grid.active_cell_count());
            const auto* bindings = config.device_blocks + first;
            constexpr int threads = 128;
            burn_cells_kernel<Network, OdeBinding>
                <<<dim3((cells + threads - 1) / threads, static_cast<unsigned>(count)), threads, 0, stream>>>(
                    state, grid, workspaces, candidates, statuses, burn_dt, eos,
                    config.controls, network, bindings);
            auto error = cudaGetLastError();
            if (error != cudaSuccess) return error;
            reduce_burn_kernel<Eos><<<dim3(1, static_cast<unsigned>(count)), 1, 0, stream>>>(
                candidates, statuses, 0, summary, bindings);
            error = cudaGetLastError();
            if (error != cudaSuccess) return error;
        }
        return cudaSuccess;
    }
    const int count = grid.active_cell_count();
    constexpr int threads = 128;
    const int blocks = (count + threads - 1) / threads;
    burn_cells_kernel<Network, OdeBinding>
        <<<blocks, threads, 0, stream>>>(
            state, grid, workspaces, candidates, statuses, burn_dt, eos,
            config.controls, network);
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
                context.burn_dt, context.eos, context.config,
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
