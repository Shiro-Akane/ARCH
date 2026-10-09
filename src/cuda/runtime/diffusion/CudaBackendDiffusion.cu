/**
 * @file CudaBackendDiffusion.cu
 * @brief Bind shared RKL descriptors and EOS views to CUDA diffusion launches.
 *
 * DiffFunction supplies stage coefficients. The binding selects stage inputs,
 * launches the shared operator/update adapters and registers AMR surface fluxes.
 * Runtime control owns slot publication, scratch lifetime and the stream fence;
 * this file does not introduce a second RKL recurrence.
 */

#include "cuda/runtime/diffusion/CudaBackendDiffusion.h"

#include "cuda/diffusion/DiffusionSolver.cuh"
#include "cuda/diffusion/DiffusionBatchKernels.cuh"
#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "core/CompensatedSum.h"
#include "physics/eos/IdealGas.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/tabular/Tabular3DEOS.h"
#include "physics/eos/tabular/Tabular4DEOS.h"

namespace arch::cuda {
namespace {

/** One bounded CTA per patch: shared scalar term, compensated lane sums and
 * compensated join. The launch never writes either resident endpoint array.
 */
__global__ void diffusion_activity_kernel(const DeviceDiffusionActivityBlock* bindings,
    DeviceDiffusionActivityResult* results)
{
    constexpr int threads = 128;
    __shared__ double signed_part[threads], absolute_part[threads];
    __shared__ std::uint64_t cells_part[threads];
    __shared__ int failed_part[threads];
    const auto binding = bindings[blockIdx.x];
    const auto& grid = binding.grid;
    const auto geometry = make_grid_geometry_view(grid);
    arch::math::CompensatedSum signed_change, absolute_change;
    std::uint64_t cells = 0;
    int failed = 0;
    for (std::uint64_t ordinal = threadIdx.x;
         ordinal < static_cast<std::uint64_t>(grid.active_cell_count()); ordinal += threads) {
        int logical = static_cast<int>(ordinal);
        const int i = grid.is + logical % (grid.ie - grid.is);
        logical /= grid.ie - grid.is;
        const int j = grid.js + logical % (grid.je - grid.js);
        const int k = grid.ks + logical / (grid.je - grid.js);
        const int index = grid.index(i, j, k);
        const auto term = DiffFlux::diffusion_energy_activity_term(
            GridMetrics::CellVolume(geometry, i, j, k),
            binding.current_energy[index], binding.seed_energy[index]);
        ++cells;
        if (!term.valid) { failed = 1; continue; }
        signed_change.add(term.signed_energy_change);
        absolute_change.add(term.absolute_energy_change);
    }
    signed_part[threadIdx.x] = signed_change.value();
    absolute_part[threadIdx.x] = absolute_change.value();
    cells_part[threadIdx.x] = cells;
    failed_part[threadIdx.x] = failed;
    __syncthreads();
    if (threadIdx.x != 0) return;
    arch::math::CompensatedSum signed_total, absolute_total;
    DeviceDiffusionActivityResult result{};
    for (int lane = 0; lane < threads; ++lane) {
        result.failed |= failed_part[lane];
        if (!std::isfinite(signed_part[lane]) || !std::isfinite(absolute_part[lane])) result.failed = 1;
        signed_total.add(signed_part[lane]); absolute_total.add(absolute_part[lane]);
        result.cells += cells_part[lane];
    }
    result.signed_energy_change = signed_total.value();
    result.absolute_energy_change = absolute_total.value();
    if (!std::isfinite(result.signed_energy_change) || !std::isfinite(result.absolute_energy_change))
        result.failed = 1;
    results[blockIdx.x] = result;
}

DiffusionWorkspaceView make_workspace(
    CudaBackendDiffusionWorkspace workspace)
{
    return {workspace.face_flux, workspace.candidates, workspace.result,
            workspace.status, workspace.species_workspace};
}

template <class Eos>
CudaBackendLaunchResult launch_diffusion_dt_impl(
    DeviceStateView state, Eos eos, SpeciesPODView species,
    DeviceGridView grid, DiffFlux::DiffusionConfigView config,
    CudaBackendDiffusionWorkspace workspace, cudaStream_t stream)
{
    const DiffusionLaunchResult result = launch_raw_diffusion_dt(
        state, eos, species, grid, config, make_workspace(workspace), stream);
    return {result.error, result.kernels_launched, true};
}

template <class Eos>
CudaBackendLaunchResult launch_diffusion_stage_impl(
    const scheduler::RklPlan& plan,
    const scheduler::RklStageDescriptor& descriptor,
    DeviceStateView state_n, DeviceStateView previous,
    DeviceStateView older, DeviceStateView output,
    DeviceStateView delta, DeviceStateView initial_delta,
    Eos eos, SpeciesPODView species, DeviceGridView grid,
    DiffFlux::DiffusionConfigView config,
    CudaBackendDiffusionWorkspace workspace,
    const CudaAmrFluxDirectionRouteView* amr_routes, double dt,
    cudaStream_t stream)
{
    const DiffFunction::RKLOrder order = plan.second_order
        ? DiffFunction::RKLOrder::Second : DiffFunction::RKLOrder::First;
    const auto coefficients = DiffFunction::get_rkl_coeffs(
        order, descriptor.stage, static_cast<int>(plan.stages.size()));
    const DiffusionLaunchResult operation = launch_bounded_diffusion_operator(
        descriptor.stage == 1 ? state_n : previous,
        descriptor.stage == 1 && plan.second_order ? initial_delta : delta,
        eos, species, grid, config, make_workspace(workspace), amr_routes,
        coefficients.tilde_mu,
        descriptor.stage == 1 && plan.second_order, stream);
    if (operation.error != cudaSuccess)
        return {operation.error, operation.kernels_launched, true};

    int registration_kernels = 0;
    if (descriptor.stage > 1 && plan.second_order && amr_routes != nullptr) {
        for (int direction = 0; direction < grid.dim; ++direction) {
            const auto registration = launch_cuda_amr_flux_register(
                amr_routes[direction], AmrFluxSource::InitialSurface,
                coefficients.gamma, stream);
            if (registration.error != cudaSuccess) {
                return {registration.error,
                        operation.kernels_launched + registration_kernels,
                        true};
            }
            registration_kernels += registration.kernels_launched;
        }
    }
    DiffusionLaunchResult update{};
    if (descriptor.stage == 1) {
        update = launch_bounded_first_rkl_stage(
            state_n, plan.second_order ? initial_delta : delta,
            output, grid, config, coefficients.tilde_mu * dt, stream);
    } else {
        update = launch_bounded_recursive_rkl_stage(
            state_n, previous, older, delta,
            plan.second_order ? initial_delta : delta,
            output, grid, config, coefficients, plan.second_order,
            dt, false, stream);
    }
    return {update.error,
            operation.kernels_launched + registration_kernels
                + update.kernels_launched,
            true};
}

} // namespace

/** Validate layout and true interior volumes without reading device energy. */
bool valid_diffusion_activity_binding(const DeviceDiffusionActivityBlock& binding)
{
    const auto& grid = binding.grid;
    if (!binding.current_energy || !binding.seed_energy || binding.current_energy == binding.seed_energy
        || !valid_hydro_grid(grid)
        || grid.geometry < static_cast<int>(DeviceGeometry::Cartesian)
        || grid.geometry > static_cast<int>(DeviceGeometry::Spherical)
        || !std::isfinite(grid.x1_min) || !std::isfinite(grid.x1_max)
        || !std::isfinite(grid.x2_min) || !std::isfinite(grid.x2_max)
        || !std::isfinite(grid.x3_min) || !std::isfinite(grid.x3_max)
        || !std::isfinite(grid.dx1) || !(grid.dx1 > 0.0)
        || (grid.dim >= 2 && (!std::isfinite(grid.dx2) || !(grid.dx2 > 0.0)))
        || (grid.dim == 3 && (!std::isfinite(grid.dx3) || !(grid.dx3 > 0.0)))) return false;
    const std::uint64_t cells = static_cast<std::uint64_t>(grid.ie - grid.is)
        * static_cast<std::uint64_t>(grid.je - grid.js) * static_cast<std::uint64_t>(grid.ke - grid.ks);
    if (cells == 0 || cells > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return false;
    const auto geometry = make_grid_geometry_view(grid);
    for (int k = grid.ks; k < grid.ke; ++k)
        for (int j = grid.js; j < grid.je; ++j)
            for (int i = grid.is; i < grid.ie; ++i) {
                const double volume = GridMetrics::CellVolume(geometry, i, j, k);
                if (!std::isfinite(volume) || !(volume > 0.0)) return false;
            }
    return true;
}

/** Upload-independent launch: all metadata preflight precedes the first CTA. */
CudaBackendLaunchResult launch_cuda_backend_diffusion_activity_batch(
    std::span<const DeviceDiffusionActivityBlock> host,
    const DeviceDiffusionActivityBlock* device, DeviceDiffusionActivityResult* results,
    cudaStream_t stream)
{
    CudaBackendLaunchResult launched{};
    if (host.empty()) return launched;
    if (!device || !results) return {cudaErrorInvalidValue, 0, true};
    for (const auto& binding : host)
        if (!valid_diffusion_activity_binding(binding)) return {cudaErrorInvalidValue, 0, true};
    constexpr std::size_t wave_limit = 1024;
    for (std::size_t first = 0; first < host.size(); first += wave_limit) {
        const auto count = std::min(wave_limit, host.size() - first);
        diffusion_activity_kernel<<<static_cast<unsigned>(count), 128, 0, stream>>>(
            device + first, results + first);
        launched.error = cudaGetLastError();
        if (launched.error != cudaSuccess) return launched;
        ++launched.kernels_launched;
    }
    return launched;
}

cudaError_t copy_cuda_backend_state_slot(
    DeviceStateView source, DeviceStateView destination,
    cudaStream_t stream)
{
    return copy_diffusion_slot(source, destination, stream);
}

CudaBackendLaunchResult copy_cuda_backend_state_slot_batch(
    std::span<const DeviceStateCopyBlock> host, const DeviceStateCopyBlock* device,
    cudaStream_t stream)
{
    CudaBackendLaunchResult result{};
    if (host.empty()) return result;
    if (!device) return {cudaErrorInvalidValue, 0, true};
    // Keep the scalar copy's full shape/alias contract before the first write.
    for (const auto& b : host)
        if (!valid_hydro_view(b.source) || !valid_hydro_view(b.destination)
            || !detail::matching_state_shape(b.source, b.destination)
            || detail::any_state_storage_alias(b.destination, b.source))
            return {cudaErrorInvalidValue, 0, true};
    constexpr std::size_t limit = 1024;
    for (std::size_t first = 0; first < host.size(); first += limit) {
        const auto count = std::min(limit, host.size() - first);
        int storage = 0;
        for (const auto& b : host.subspan(first, count)) storage = std::max(storage, b.source.total_size);
        detail::state_copy_batch_kernel<<<
            dim3(detail::hydro_launch_blocks(storage, 128), static_cast<unsigned>(count)), 128, 0, stream>>>(
                device + first);
        result.error = cudaGetLastError();
        if (result.error != cudaSuccess) return result;
        ++result.kernels_launched;
    }
    return result;
}

#define ARCH_DEFINE_BACKEND_DIFFUSION(EOS) \
    CudaBackendLaunchResult launch_cuda_backend_diffusion_dt( \
        DeviceStateView state, EOS eos, SpeciesPODView species, \
        DeviceGridView grid, DiffFlux::DiffusionConfigView config, \
        CudaBackendDiffusionWorkspace workspace, cudaStream_t stream) \
    { \
        return launch_diffusion_dt_impl( \
            state, eos, species, grid, config, workspace, stream); \
    } \
    CudaBackendLaunchResult launch_cuda_backend_diffusion_stage( \
        const scheduler::RklPlan& plan, \
        const scheduler::RklStageDescriptor& descriptor, \
        DeviceStateView state_n, DeviceStateView previous, \
        DeviceStateView older, DeviceStateView output, \
        DeviceStateView delta, DeviceStateView initial_delta, \
        EOS eos, SpeciesPODView species, DeviceGridView grid, \
        DiffFlux::DiffusionConfigView config, \
        CudaBackendDiffusionWorkspace workspace, \
        const CudaAmrFluxDirectionRouteView* amr_routes, double dt, \
        cudaStream_t stream) \
    { \
        return launch_diffusion_stage_impl( \
            plan, descriptor, state_n, previous, older, output, delta, \
            initial_delta, eos, species, grid, config, workspace, amr_routes, \
            dt, stream); \
    } \
    CudaBackendLaunchResult launch_cuda_backend_diffusion_dt_batch( \
        std::span<const DeviceDiffusionBatchBlock> host, const DeviceDiffusionBatchBlock* device, \
        EOS eos, SpeciesPODView species, DiffFlux::DiffusionConfigView config, cudaStream_t stream) \
    { \
        return launch_diffusion_dt_batch(host, device, eos, species, config, stream); \
    } \
    CudaBackendLaunchResult launch_cuda_backend_diffusion_stage_batch( \
        const scheduler::RklPlan& plan, const scheduler::RklStageDescriptor& descriptor, \
        std::span<const DeviceDiffusionBatchBlock> host, const DeviceDiffusionBatchBlock* device, \
        EOS eos, SpeciesPODView species, DiffFlux::DiffusionConfigView config, double dt, cudaStream_t stream) \
    { \
        return launch_diffusion_stage_batch(plan, descriptor, host, device, eos, species, config, dt, stream); \
    }

ARCH_DEFINE_BACKEND_DIFFUSION(IdealGasView)
ARCH_DEFINE_BACKEND_DIFFUSION(HelmEosView)
ARCH_DEFINE_BACKEND_DIFFUSION(Tabular3DEOSView)
ARCH_DEFINE_BACKEND_DIFFUSION(Tabular4DEOSView)

#undef ARCH_DEFINE_BACKEND_DIFFUSION

} // namespace arch::cuda
