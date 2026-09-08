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
#include "physics/eos/IdealGas.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"

namespace arch::cuda {
namespace {

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

cudaError_t copy_cuda_backend_state_slot(
    DeviceStateView source, DeviceStateView destination,
    cudaStream_t stream)
{
    return copy_diffusion_slot(source, destination, stream);
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
    }

ARCH_DEFINE_BACKEND_DIFFUSION(IdealGasView)
ARCH_DEFINE_BACKEND_DIFFUSION(HelmEosView)
ARCH_DEFINE_BACKEND_DIFFUSION(Tabular3DEOSView)
ARCH_DEFINE_BACKEND_DIFFUSION(Tabular4DEOSView)

#undef ARCH_DEFINE_BACKEND_DIFFUSION

} // namespace arch::cuda
