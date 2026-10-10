/**
 * @file RefinementIndicators.cu
 * @brief Device traversal and block reduction for AMR refinement indicators.
 *
 * Optional thermodynamic fields are evaluated through shared EOS views, then
 * amr::indicator::cell_error evaluates the selected stencils. On the Native RZ
 * chart the shared density/inertia closure and its physical center baseline
 * replace ordinary point EOS on raw V/W conserved means; Existing input keeps
 * its original padded point-EOS arithmetic. A final device reduction preserves
 * nonfinite/EOS failures. Launches enqueue work only; runtime control owns
 * buffers, synchronization and refine/derefine decisions.
 */

#include "cuda/amr/RefinementIndicators.h"
#include "cuda/common/DeviceEosStatus.h"
#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "numerics/state/RzNativeClosure.h"
#include "physics/diagnostics/JeansDiagnostics.h"

namespace arch::cuda {
namespace {

// The device grid mirrors the Host chart identity. The Native full-ring RZ
// chart is the only one whose conserved means are V/W means rather than
// ordinary point states; it is read from the actual grid, not a backend flag.
ARCH_INLINE bool native_semantics(DeviceGridView grid)
{
    return static_cast<int>(grid.semantics)
        == static_cast<int>(GridMetrics::GeometrySemantics::AxisymmetricRz);
}

// Host Native closure: std::clamp(i-1,0,nx-3). The explicit real three-column
// radial support never guesses a halo beyond the supplied begin.
ARCH_INLINE int native_support_begin(int i, int nx)
{
    const int last = nx - 3;
    return i - 1 < 0 ? 0 : (i - 1 > last ? last : i - 1);
}

// The same actual logical layout preflight as the Host completed-patch Native
// traversal: real radial support, at least one logical ghost layer, one logical
// z plane and an x padding that stays storage-only.
ARCH_INLINE bool valid_native_layout(DeviceGridView grid)
{
    return grid.total_x >= 3 && grid.total_y >= 1 && grid.stride_y >= grid.total_x
        && grid.stride_z > 0 && grid.total_y <= grid.stride_z / grid.stride_y
        && grid.total_z == 1 && grid.ng >= 1
        && (grid.total_x - 1) + grid.stride_y * (grid.total_y - 1) < grid.total_size;
}

// Use the selected EOS and total positive density on active accepted cells.
// No density floor, mean-density subtraction or copied sound-speed formula.
template<class Eos>
__global__ void jeans_resolution_kernel(DeviceStateView state, DeviceGridView grid,
                                        Eos eos, DeviceJeansWorkspace workspace)
{
    const int linear = blockIdx.x * blockDim.x + threadIdx.x;
    if (linear >= grid.active_cell_count()) return;
    const int cell = grid.active_cell(linear);
    const int k = cell / grid.stride_z;
    const int j = (cell - k * grid.stride_z) / grid.stride_y;
    const int i = cell - k * grid.stride_z - j * grid.stride_y;
    double* fractions = state.n_species
        ? workspace.composition + static_cast<std::size_t>(cell) * state.n_species
        : nullptr;
    for (int species = 0; species < state.n_species; ++species)
        fractions[species] = state.species(species, cell);
    // The original point arithmetic of the Existing route, evaluated on the
    // accepted state selected below for the actual chart.
    const auto observe = [&](const auto& value) {
        const double pressure = eos.get_pressure(value, fractions);
        const double sound = eos.get_sound_speed(value, pressure, fractions);
        const auto resolution = std::isfinite(pressure) && pressure > 0.0
            && std::isfinite(sound) && sound > 0.0
            ? JeansDiagnostics::evaluate_cell(value.rho,
                sound * sound, make_grid_geometry_view(grid), i, j)
            : JeansDiagnostics::Resolution{};
        workspace.cell_resolution[linear] =
            resolution.status == JeansDiagnostics::Status::valid
            ? resolution.cells : std::numeric_limits<double>::quiet_NaN();
    };
    if (!native_semantics(grid)) {
        observe(state.load(cell));
        return;
    }
    // Native full-ring RZ: the actual accepted conserved means are V/W means,
    // so the accepted observer reuses the exact shared Host closure and its
    // explicit real radial support instead of point EOS on raw means. Default
    // leaf bounds derive only the numerical mean; genuine configured source
    // acceptance stays owned by Runtime. An invalid closure is not repaired.
    const auto closure = RzThermodynamics::make_cell_supported(
        [&state](int index) { return state.load(index); }, cell,
        make_grid_geometry_view(grid), i, native_support_begin(i, grid.total_x));
    if (!closure.valid()) {
        workspace.cell_resolution[linear] = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    observe(closure.effective_mean);
}

__global__ void jeans_minimum_kernel(DeviceJeansWorkspace workspace, int count)
{
    if (blockIdx.x || threadIdx.x) return;
    double minimum = std::numeric_limits<double>::infinity();
    if (*workspace.eos_status != 0) {
        *workspace.block_minimum = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    for (int cell = 0; cell < count; ++cell) {
        const double value = workspace.cell_resolution[cell];
        if (!std::isfinite(value) || value <= 0.0) {
            *workspace.block_minimum = std::numeric_limits<double>::quiet_NaN();
            return;
        }
        minimum = std::min(minimum, value);
    }
    *workspace.block_minimum = minimum;
}

template<class Eos>
cudaError_t launch_jeans(DeviceStateView state, DeviceGridView grid, Eos eos,
                         DeviceJeansWorkspace workspace, cudaStream_t stream)
{
    if (!valid_hydro_view(state) || !valid_hydro_grid(grid)
        || (grid.geometry != static_cast<int>(DeviceGeometry::Cartesian)
            && !native_semantics(grid))
        || state.total_size != grid.total_size || grid.active_cell_count() <= 0
        || !workspace.cell_resolution || !workspace.block_minimum
        || !workspace.eos_status || (state.n_species && !workspace.composition))
        return cudaErrorInvalidValue;
    // The Native observer reads the actual two-dimensional full-ring chart
    // through its explicit real radial support; general Cylindrical or Spherical
    // identity is not accepted here and no geometry enum value is guessed.
    // Storage padding is never an EOS cell.
    if (native_semantics(grid) && (grid.dim != 2 || !valid_native_layout(grid)))
        return cudaErrorInvalidValue;
    if constexpr (requires { eos.species.size(); }) {
        if (eos.species.size() > 0 && eos.species.size() != state.n_species)
            return cudaErrorInvalidValue;
    }
    auto error = cudaMemsetAsync(workspace.eos_status, 0, sizeof(int), stream);
    if (error != cudaSuccess) return error;
    int minimum_grid = 0, threads = 0;
    error = cudaOccupancyMaxPotentialBlockSize(&minimum_grid, &threads,
        jeans_resolution_kernel<Eos>);
    if (error != cudaSuccess) return error;
    jeans_resolution_kernel<<<detail::hydro_launch_blocks(grid.active_cell_count(), threads),
        threads, 0, stream>>>(state, grid, bind_device_eos_status(eos, workspace.eos_status), workspace);
    error = cudaGetLastError();
    if (error != cudaSuccess) return error;
    jeans_minimum_kernel<<<1, 1, 0, stream>>>(workspace, grid.active_cell_count());
    return cudaGetLastError();
}

// Shared Native closure failure: the private wave latch is sticky 0/1 and the
// borrowed planes stay invalid, even for a ghost outside the final stencil.
ARCH_DEVICE ARCH_FORCE_INLINE void native_thermodynamics_failure(DeviceStateView state,
    DeviceIndicatorWorkspace workspace, int cell)
{
    const double invalid = std::numeric_limits<double>::quiet_NaN();
    atomicExch(workspace.eos_status, 1);
    if (workspace.thermodynamics != nullptr) {
        workspace.thermodynamics[cell] = invalid;
        workspace.thermodynamics[state.total_size + cell] = invalid;
        workspace.thermodynamics[2 * state.total_size + cell] = invalid;
    }
    if (workspace.physical_velocity[0] != nullptr)
        for (int component = 0; component < 3; ++component)
            workspace.physical_velocity[component][cell] = invalid;
}

template<class Eos>
__global__ void thermodynamics_kernel(DeviceStateView state, DeviceGridView grid, Eos eos,
                                      DeviceIndicatorWorkspace workspace,
                                      const DeviceIndicatorBatchBlock* batch = nullptr)
{
    if (batch) {
        state = batch[blockIdx.y].state;
        grid = batch[blockIdx.y].grid;
        workspace = batch[blockIdx.y].workspace;
        eos = bind_device_eos_status(eos, workspace.eos_status);
        // A wave launches when any block needs a mean-thermodynamics or Native
        // physical evaluation; each block keeps its own route and latch.
        if (!workspace.pressure && !workspace.temperature && !workspace.gamma1
            && workspace.physical_velocity[0] == nullptr) return;
    }
    const int cell = blockIdx.x * blockDim.x + threadIdx.x;
    if (cell >= state.total_size) return;
    if (!native_semantics(grid)) {
        // Original Existing arithmetic: every padded storage cell is one
        // ordinary point EOS sample, exactly as before.
        double* composition = state.n_species > 0
            ? workspace.composition + static_cast<std::size_t>(cell) * state.n_species : nullptr;
        for (int species = 0; species < state.n_species; ++species)
            composition[species] = state.species(species, cell);
        const auto value = amr::indicator::thermodynamics(state.load(cell), composition,
            eos, workspace.pressure, workspace.temperature, workspace.gamma1);
        workspace.thermodynamics[cell] = value.pressure;
        workspace.thermodynamics[state.total_size + cell] = value.temperature;
        workspace.thermodynamics[2 * state.total_size + cell] = value.gamma1;
        return;
    }
    // Native chart: only genuine logical cells are physical. The x row
    // remainder is storage padding, never an EOS sample or density support.
    const int k = cell / grid.stride_z;
    const int j = (cell - k * grid.stride_z) / grid.stride_y;
    const int i = cell - k * grid.stride_z - j * grid.stride_y;
    if (i >= grid.total_x || j >= grid.total_y || k >= grid.total_z) return;
    double* composition = state.n_species > 0
        ? workspace.composition + static_cast<std::size_t>(cell) * state.n_species : nullptr;
    for (int species = 0; species < state.n_species; ++species)
        composition[species] = state.species(species, cell);
    const auto geometry = make_grid_geometry_view(grid);
    const auto read = [&state](int index) { return state.load(index); };
    // The Host Native traversal closure: shared density/inertia means with the
    // explicit real three-column radial support clamp(i-1,0,nx-3).
    const auto closure = RzThermodynamics::make_cell_supported(read, cell, geometry, i,
        native_support_begin(i, grid.total_x), workspace.bounds);
    if (!closure.valid() || arch::state::validate_eos(closure.effective_mean, composition,
            state.n_species, workspace.bounds, eos) != arch::state::Status::valid) {
        native_thermodynamics_failure(state, workspace, cell);
        return;
    }
    const auto values = amr::indicator::thermodynamics(closure.effective_mean, composition,
        eos, workspace.pressure, workspace.temperature, workspace.gamma1);
    workspace.thermodynamics[cell] = values.pressure;
    workspace.thermodynamics[state.total_size + cell] = values.temperature;
    workspace.thermodynamics[2 * state.total_size + cell] = values.gamma1;
    if (workspace.physical_velocity[0] == nullptr) return;
    // Same conservative physical center baseline as the Host Native traversal.
    const auto point = RzThermodynamics::base_point(closure, geometry.GetCellCenterX(i));
    if (arch::state::validate_eos(point, composition, state.n_species, workspace.bounds, eos)
        != arch::state::Status::valid) {
        native_thermodynamics_failure(state, workspace, cell);
        return;
    }
    workspace.physical_velocity[0][cell] = point.mom_u / point.rho;
    workspace.physical_velocity[1][cell] = point.mom_v / point.rho;
    workspace.physical_velocity[2][cell] = point.mom_w / point.rho;
    for (int component = 0; component < 3; ++component)
        if (!std::isfinite(workspace.physical_velocity[component][cell])) {
            native_thermodynamics_failure(state, workspace, cell);
            return;
        }
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
        state.n_species,
        {workspace.physical_velocity[0], workspace.physical_velocity[1],
         workspace.physical_velocity[2]}};
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
    const bool thermodynamics = workspace.pressure || workspace.temperature || workspace.gamma1;
    const bool physical = workspace.physical_velocity[0] != nullptr
        || workspace.physical_velocity[1] != nullptr
        || workspace.physical_velocity[2] != nullptr;
    // Borrowed physical planes are all-or-none and belong to the Native chart
    // only; a partial request or an Existing pointer is a binding error.
    if (physical && !(workspace.physical_velocity[0] && workspace.physical_velocity[1]
            && workspace.physical_velocity[2] && native_semantics(grid)))
        return false;
    if (!valid_hydro_view(state) || !valid_hydro_grid(grid)
        || state.total_size != grid.total_size || workspace.selection_count < 0
        || (workspace.selection_count != 0 && workspace.selection == nullptr)
        || workspace.cell_errors == nullptr || workspace.block_error == nullptr)
        return false;
    if (!thermodynamics && !physical) return true;
    // Both routes need the shared arena planes, the private EOS latch and the
    // per-cell composition scratch the kernel writes.
    if (workspace.thermodynamics == nullptr || workspace.eos_status == nullptr
        || (state.n_species != 0 && workspace.composition == nullptr))
        return false;
    if (!native_semantics(grid)) return true;
    // The Native traversal borrows the actual chart, the actual bounds and a
    // genuine logical layout: storage pitch padding is never an EOS cell.
    return arch::state::valid_bounds(workspace.bounds)
        && workspace.bounds.density == workspace.density_floor && valid_native_layout(grid);
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
    const bool physical = workspace.physical_velocity[0] != nullptr;
    if (workspace.pressure || workspace.temperature || workspace.gamma1 || physical) {
        error = cudaOccupancyMaxPotentialBlockSize(&minimum_grid, &threads,
            thermodynamics_kernel<Eos>);
        if (error != cudaSuccess) return error;
        thermodynamics_kernel<<<detail::hydro_launch_blocks(state.total_size, threads), threads, 0, stream>>>(
            state, grid, bind_device_eos_status(eos, workspace.eos_status), workspace);
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
            // A Native physical-only request still needs the shared closure and
            // its mean EOS gate, so the same optional launch serves it.
            thermo |= b.workspace.pressure || b.workspace.temperature || b.workspace.gamma1
                || b.workspace.physical_velocity[0] != nullptr;
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
                static_cast<unsigned>(count)), thermo_threads, 0, stream>>>(b.state, b.grid, eos, b.workspace, bindings);
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

#define ARCH_DEFINE_JEANS_RESOLUTION(EOS) \
cudaError_t launch_cuda_jeans_resolution( \
    DeviceStateView state, DeviceGridView grid, EOS eos, \
    DeviceJeansWorkspace workspace, cudaStream_t stream) \
{ return launch_jeans(state, grid, eos, workspace, stream); }
ARCH_DEFINE_JEANS_RESOLUTION(IdealGasView)
ARCH_DEFINE_JEANS_RESOLUTION(HelmEosView)
ARCH_DEFINE_JEANS_RESOLUTION(Tabular3DEOSView)
ARCH_DEFINE_JEANS_RESOLUTION(Tabular4DEOSView)
#undef ARCH_DEFINE_JEANS_RESOLUTION

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
