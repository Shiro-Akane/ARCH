/**
 * @file RefinementIndicators.cu
 * @brief Device traversal and block reduction for AMR refinement indicators.
 *
 * Optional thermodynamic fields are evaluated through shared EOS views, then
 * amr::indicator::cell_error evaluates the selected stencils. A final device
 * reduction preserves nonfinite/EOS failures. Launches enqueue work only;
 * runtime control owns buffers, synchronization and refine/derefine decisions.
 */

#include "RefinementIndicators.h"
#include "cuda/common/DeviceEosStatus.h"
#include "cuda/hydro/GridGeometryAdapter.cuh"

namespace arch::cuda {
namespace {

template<class Eos>
__global__ void thermodynamics_kernel(DeviceStateView state, Eos eos,
                                      DeviceIndicatorWorkspace workspace,
                                      const DeviceIndicatorBatchBlock* batch = nullptr)
{
    if (batch) {
        state = batch[blockIdx.y].state;
        workspace = batch[blockIdx.y].workspace;
        eos = bind_device_eos_status(eos, workspace.eos_status);
        if (!workspace.pressure && !workspace.temperature && !workspace.gamma1) return;
    }
    const int cell = blockIdx.x * blockDim.x + threadIdx.x;
    if (cell >= state.total_size) return;
    double* composition = state.n_species > 0
        ? workspace.composition + static_cast<std::size_t>(cell) * state.n_species : nullptr;
    for (int species = 0; species < state.n_species; ++species)
        composition[species] = state.species(species, cell);
    const auto value = amr::indicator::thermodynamics(state.load(cell), composition,
        eos, workspace.pressure, workspace.temperature, workspace.gamma1);
    workspace.thermodynamics[cell] = value.pressure;
    workspace.thermodynamics[state.total_size + cell] = value.temperature;
    workspace.thermodynamics[2 * state.total_size + cell] = value.gamma1;
}

__global__ void indicators_kernel(DeviceStateView state, DeviceGridView grid,
                                  DeviceIndicatorWorkspace workspace,
                                  const DeviceIndicatorBatchBlock* batch = nullptr)
{
    if (batch) {
        state = batch[blockIdx.y].state;
        grid = batch[blockIdx.y].grid;
        workspace = batch[blockIdx.y].workspace;
    }
    const int linear = blockIdx.x * blockDim.x + threadIdx.x;
    if (linear >= grid.active_cell_count()) return;
    const int cell = grid.active_cell(linear);
    const int k = cell / grid.stride_z;
    const int j = (cell - k * grid.stride_z) / grid.stride_y;
    const int i = cell - k * grid.stride_z - j * grid.stride_y;
    const auto* thermo = workspace.thermodynamics;
    const amr::indicator::StateView view{
        state.rho, {state.mom_u, state.mom_v, state.mom_w}, state.eng,
        state.enuc_rate, state.mass_fractions, thermo,
        thermo ? thermo + state.total_size : nullptr,
        thermo ? thermo + 2 * state.total_size : nullptr, state.total_size,
        grid.is, grid.ie, grid.js, grid.je, grid.ks, grid.ke, workspace.density_floor,
        state.n_species};
    workspace.cell_errors[linear] = amr::indicator::cell_error(view,
        make_grid_geometry_view(grid), workspace.selection, workspace.selection_count, i, j, k);
}

__global__ void maximum_kernel(const double* errors, int count, double* result,
                               const int* eos_status,
                               const DeviceIndicatorBatchBlock* batch = nullptr)
{
    if (blockIdx.x || threadIdx.x) return;
    if (batch) {
        const auto& b = batch[blockIdx.y];
        errors = b.workspace.cell_errors;
        count = b.grid.active_cell_count();
        result = b.workspace.block_error;
        eos_status = b.workspace.eos_status;
    }
    // A thermodynamic query can fail in an intermediate inversion or a ghost
    // that does not enter the selected stencil. Host evaluation still throws;
    // retain that failure even when every final cell-error value is finite.
    if (eos_status != nullptr && *eos_status != 0) {
        *result = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    double maximum = 0.0;
    for (int cell = 0; cell < count; ++cell) {
        if (!std::isfinite(errors[cell])) {
            *result = std::numeric_limits<double>::quiet_NaN();
            return;
        }
        maximum = std::max(maximum, errors[cell]);
    }
    *result = maximum;
}

__global__ void reset_batch_status(const DeviceIndicatorBatchBlock* batch)
{
    if (threadIdx.x == 0 && batch[blockIdx.y].workspace.eos_status)
        *batch[blockIdx.y].workspace.eos_status = 0;
}

bool valid_indicator_binding(DeviceStateView state, DeviceGridView grid,
                             DeviceIndicatorWorkspace workspace)
{
    return valid_hydro_view(state) && valid_hydro_grid(grid)
        && state.total_size == grid.total_size && workspace.selection_count >= 0
        && (workspace.selection_count == 0 || workspace.selection != nullptr)
        && workspace.cell_errors != nullptr && workspace.block_error != nullptr
        && (!(workspace.pressure || workspace.temperature || workspace.gamma1)
            || (workspace.thermodynamics && workspace.eos_status
                && (state.n_species == 0 || workspace.composition)));
}

template<class Eos>
cudaError_t launch(DeviceStateView state, DeviceGridView grid, Eos eos,
                   DeviceIndicatorWorkspace workspace, cudaStream_t stream)
{
    if (!valid_indicator_binding(state, grid, workspace))
        return cudaErrorInvalidValue;
    int minimum_grid = 0, threads = 0;
    cudaError_t error = cudaSuccess;
    if (workspace.eos_status != nullptr) {
        error = cudaMemsetAsync(workspace.eos_status, 0, sizeof(int), stream);
        if (error != cudaSuccess) return error;
    }
    if (workspace.pressure || workspace.temperature || workspace.gamma1) {
        error = cudaOccupancyMaxPotentialBlockSize(&minimum_grid, &threads,
            thermodynamics_kernel<Eos>);
        if (error != cudaSuccess) return error;
        thermodynamics_kernel<<<detail::hydro_launch_blocks(state.total_size, threads), threads, 0, stream>>>(
            state, bind_device_eos_status(eos, workspace.eos_status), workspace);
        error = cudaGetLastError();
        if (error != cudaSuccess) return error;
    }
    error = cudaOccupancyMaxPotentialBlockSize(&minimum_grid, &threads, indicators_kernel);
    if (error != cudaSuccess) return error;
    indicators_kernel<<<detail::hydro_launch_blocks(grid.active_cell_count(), threads), threads, 0, stream>>>(state, grid, workspace);
    error = cudaGetLastError();
    if (error != cudaSuccess) return error;
    maximum_kernel<<<1, 1, 0, stream>>>(workspace.cell_errors,
        grid.active_cell_count(), workspace.block_error, workspace.eos_status);
    return cudaGetLastError();
}

template<class Eos>
CudaBackendLaunchResult launch_batch(std::span<const DeviceIndicatorBatchBlock> host,
    const DeviceIndicatorBatchBlock* device, std::size_t wave_capacity,
    Eos eos, cudaStream_t stream)
{
    CudaBackendLaunchResult result{};
    if (host.empty()) return result;
    if (!device || wave_capacity == 0 || wave_capacity > 1024)
        return {cudaErrorInvalidValue, 0, true};
    for (const auto& b : host)
        if (!valid_indicator_binding(b.state, b.grid, b.workspace))
            return {cudaErrorInvalidValue, 0, true};
    const auto record = [&] {
        result.error = cudaGetLastError();
        if (result.error == cudaSuccess) ++result.kernels_launched;
        return result.error == cudaSuccess;
    };
    int minimum_grid = 0, indicator_threads = 0, thermo_threads = 0;
    result.error = cudaOccupancyMaxPotentialBlockSize(&minimum_grid, &indicator_threads, indicators_kernel);
    if (result.error != cudaSuccess) return result;
    for (std::size_t first = 0; first < host.size(); first += wave_capacity) {
        const auto count = std::min(wave_capacity, host.size() - first);
        const auto wave = host.subspan(first, count);
        int storage = 0, cells = 0;
        bool thermo = false;
        for (const auto& b : wave) {
            storage = std::max(storage, b.state.total_size);
            cells = std::max(cells, b.grid.active_cell_count());
            thermo |= b.workspace.pressure || b.workspace.temperature || b.workspace.gamma1;
        }
        if (thermo && thermo_threads == 0) {
            result.error = cudaOccupancyMaxPotentialBlockSize(&minimum_grid, &thermo_threads,
                thermodynamics_kernel<Eos>);
            if (result.error != cudaSuccess) return result;
        }
        const auto* bindings = device + first;
        const auto& b = wave.front();
        const dim3 reduction_grid(1, static_cast<unsigned>(count));
        reset_batch_status<<<reduction_grid, 1, 0, stream>>>(bindings);
        if (!record()) return result;
        if (thermo) {
            thermodynamics_kernel<<<dim3(detail::hydro_launch_blocks(storage, thermo_threads),
                static_cast<unsigned>(count)), thermo_threads, 0, stream>>>(b.state, eos, b.workspace, bindings);
            if (!record()) return result;
        }
        indicators_kernel<<<dim3(detail::hydro_launch_blocks(cells, indicator_threads),
            static_cast<unsigned>(count)), indicator_threads, 0, stream>>>(b.state, b.grid, b.workspace, bindings);
        if (!record()) return result;
        maximum_kernel<<<reduction_grid, 1, 0, stream>>>(nullptr, 0, nullptr, nullptr, bindings);
        if (!record()) return result;
    }
    return result;
}

} // namespace

#define ARCH_DEFINE_REFINEMENT_INDICATORS(EOS) \
cudaError_t launch_cuda_refinement_indicators( \
    DeviceStateView state, DeviceGridView grid, EOS eos, \
    DeviceIndicatorWorkspace workspace, cudaStream_t stream) \
{ return launch(state, grid, eos, workspace, stream); }
ARCH_DEFINE_REFINEMENT_INDICATORS(IdealGasView)
ARCH_DEFINE_REFINEMENT_INDICATORS(HelmEosView)
ARCH_DEFINE_REFINEMENT_INDICATORS(Tabular3DEOSView)
ARCH_DEFINE_REFINEMENT_INDICATORS(Tabular4DEOSView)
#undef ARCH_DEFINE_REFINEMENT_INDICATORS

#define ARCH_DEFINE_BATCH_REFINEMENT_INDICATORS(EOS) \
CudaBackendLaunchResult launch_cuda_refinement_indicators_batch( \
    std::span<const DeviceIndicatorBatchBlock> host, const DeviceIndicatorBatchBlock* device, \
    std::size_t blocks_per_wave, EOS eos, cudaStream_t stream) \
{ return launch_batch(host, device, blocks_per_wave, eos, stream); }
ARCH_DEFINE_BATCH_REFINEMENT_INDICATORS(IdealGasView)
ARCH_DEFINE_BATCH_REFINEMENT_INDICATORS(HelmEosView)
ARCH_DEFINE_BATCH_REFINEMENT_INDICATORS(Tabular3DEOSView)
ARCH_DEFINE_BATCH_REFINEMENT_INDICATORS(Tabular4DEOSView)
#undef ARCH_DEFINE_BATCH_REFINEMENT_INDICATORS

} // namespace arch::cuda
