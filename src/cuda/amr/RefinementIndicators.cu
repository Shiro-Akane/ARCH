#include "RefinementIndicators.h"
#include "cuda/common/DeviceEosStatus.h"
#include "cuda/hydro/GridGeometryAdapter.cuh"

namespace arch::cuda {
namespace {

template<class Eos>
__global__ void thermodynamics_kernel(DeviceStateView state, Eos eos,
                                      DeviceIndicatorWorkspace workspace)
{
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
                                  DeviceIndicatorWorkspace workspace)
{
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
                               const int* eos_status)
{
    if (blockIdx.x || threadIdx.x) return;
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

template<class Eos>
cudaError_t launch(DeviceStateView state, DeviceGridView grid, Eos eos,
                   DeviceIndicatorWorkspace workspace, cudaStream_t stream)
{
    if (!valid_hydro_view(state) || !valid_hydro_grid(grid)
        || state.total_size != grid.total_size || workspace.selection_count < 0
        || (workspace.selection_count > 0 && workspace.selection == nullptr)
        || workspace.cell_errors == nullptr || workspace.block_error == nullptr)
        return cudaErrorInvalidValue;
    int minimum_grid = 0, threads = 0;
    cudaError_t error = cudaSuccess;
    if (workspace.eos_status != nullptr) {
        error = cudaMemsetAsync(workspace.eos_status, 0, sizeof(int), stream);
        if (error != cudaSuccess) return error;
    }
    if (workspace.pressure || workspace.temperature || workspace.gamma1) {
        if (!workspace.thermodynamics || workspace.eos_status == nullptr
            || (state.n_species > 0 && !workspace.composition))
            return cudaErrorInvalidValue;
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

} // namespace arch::cuda
