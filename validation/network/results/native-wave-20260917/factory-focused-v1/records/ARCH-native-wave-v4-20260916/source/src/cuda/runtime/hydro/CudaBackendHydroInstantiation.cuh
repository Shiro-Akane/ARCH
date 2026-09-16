/**
 * @file CudaBackendHydroInstantiation.cuh
 * @brief Instantiate common CUDA hydro launch routes for one EOS type.
 *
 * Registered reconstruction and flux visitors bind the shared numerical
 * policies to device kernels. Views, workspaces and stream are borrowed from
 * the runtime, which checks completion and publishes slot/ghost validity.
 */

#pragma once

#include "cuda/runtime/hydro/CudaBackendHydro.h"

#include "cuda/hydro/CheckedHydroEos.cuh"
#include "cuda/hydro/HydroIntegratorPolicies.cuh"
#include "cuda/hydro/HydroBatchKernels.cuh"

#ifndef ARCH_CUDA_HYDRO_EOS_TYPE
#error "a CUDA Hydro EOS owner must define ARCH_CUDA_HYDRO_EOS_TYPE"
#endif

namespace arch::cuda {
namespace {

template <class Eos>
cudaError_t launch_hydro_dt_impl(
    DeviceStateView state, DeviceGridView grid, Eos eos, double cfl,
    CudaHydroWorkspaceView workspace, cudaStream_t stream)
{
    return launch_compute_hydro_dt(
        state, grid, make_checked_hydro_eos(eos, workspace.cfl_status),
        cfl, workspace, stream);
}

template <class Eos>
struct HydroStageLaunchVisitor {
    DeviceStateView old_state, input, output, delta, face_flux;
    DeviceGridView grid;
    Eos eos;
    double entropy_fix_coefficient, density_floor;
    double minimum_internal_energy, maximum_internal_energy;
    const CudaAmrFluxDirectionRouteView* amr_routes;
    const scheduler::StageDescriptor& descriptor;
    double dt;
    int* eos_status;
    cudaStream_t stream;
    SpeciesWorkspaceView species_workspace;
    CudaBackendLaunchResult& result;
    Physical::Gravity::ExternalGravityView gravity;

    // Registry visitors instantiate this named callback; every selected
    // reconstruction/flux combination uses the same checked-EOS route.
    template <class Reconstruction, class Flux>
    void operator()()
    {
        if (eos_status == nullptr) return;
        result.error = cudaMemsetAsync(eos_status, 0, sizeof(int), stream);
        if (result.error != cudaSuccess) return;
        result.error = launch_bounded_hydro_stage<Reconstruction, Flux>(
            old_state, input, output, delta, face_flux, grid,
            make_checked_hydro_eos(eos, eos_status),
            entropy_fix_coefficient, density_floor,
            minimum_internal_energy, maximum_internal_energy,
            amr_routes, descriptor, dt, stream,
            result.kernels_launched, species_workspace, gravity);
    }
};

template <class Eos>
CudaBackendLaunchResult launch_hydro_stage_impl(
    const dispatch::ResolvedExecutionPlan& plan, DeviceStateView old_state,
    DeviceStateView input, DeviceStateView output, DeviceStateView delta,
    DeviceStateView face_flux, DeviceGridView grid, Eos eos,
    double entropy_fix_coefficient, double density_floor,
    double minimum_internal_energy,
    double maximum_internal_energy,
    const CudaAmrFluxDirectionRouteView* amr_routes,
    const scheduler::StageDescriptor& descriptor, double dt,
    int* eos_status, cudaStream_t stream, SpeciesWorkspaceView species_workspace,
    Physical::Gravity::ExternalGravityView gravity)
{
    CudaBackendLaunchResult result{cudaErrorInvalidValue, 0, false};
    HydroStageLaunchVisitor<Eos> visitor{
        old_state, input, output, delta, face_flux, grid, eos,
        entropy_fix_coefficient, density_floor,
        minimum_internal_energy, maximum_internal_energy,
        amr_routes, descriptor, dt, eos_status, stream, species_workspace, result, gravity};
    result.route_found = visit_cuda_hydro_route(plan, visitor);
    return result;
}

template <class Eos>
struct HydroBatchLaunchVisitor {
    std::span<const DeviceHydroBatchBlock> host;
    const DeviceHydroBatchBlock* device;
    Eos eos;
    double coefficient, density_floor, min_e, max_e;
    const scheduler::StageDescriptor& descriptor;
    double dt;
    cudaStream_t stream;
    SpeciesWorkspaceView workspace;
    Physical::Gravity::ExternalGravityView gravity;
    CudaBackendLaunchResult& result;

    template <class Reconstruction, class Flux>
    void operator()()
    {
        result = launch_hydro_batch<Reconstruction, Flux>(host, device, eos,
            coefficient, density_floor, min_e, max_e, descriptor, dt,
            stream, workspace, gravity);
    }
};

} // namespace

#define ARCH_DEFINE_BACKEND_HYDRO(EOS) \
    cudaError_t launch_cuda_backend_hydro_dt( \
        DeviceStateView state, DeviceGridView grid, EOS eos, double cfl, \
        CudaHydroWorkspaceView workspace, cudaStream_t stream) \
    { \
        return launch_hydro_dt_impl( \
            state, grid, eos, cfl, workspace, stream); \
    } \
    CudaBackendLaunchResult launch_cuda_backend_hydro_stage( \
        const dispatch::ResolvedExecutionPlan& plan, DeviceStateView old_state, \
        DeviceStateView input, DeviceStateView output, DeviceStateView delta, \
        DeviceStateView face_flux, DeviceGridView grid, EOS eos, \
        double entropy_fix_coefficient, double density_floor, \
        double minimum_internal_energy, \
        double maximum_internal_energy, \
        const CudaAmrFluxDirectionRouteView* amr_routes, \
        const scheduler::StageDescriptor& descriptor, double dt, \
        int* eos_status, cudaStream_t stream, SpeciesWorkspaceView species_workspace, \
        Physical::Gravity::ExternalGravityView gravity) \
    { \
        return launch_hydro_stage_impl( \
            plan, old_state, input, output, delta, face_flux, grid, eos, \
            entropy_fix_coefficient, density_floor, minimum_internal_energy, \
            maximum_internal_energy, \
            amr_routes, descriptor, dt, eos_status, stream, species_workspace, gravity); \
    }

ARCH_DEFINE_BACKEND_HYDRO(ARCH_CUDA_HYDRO_EOS_TYPE)

CudaBackendLaunchResult launch_cuda_backend_hydro_stage_batch(
    const dispatch::ResolvedExecutionPlan& plan,
    std::span<const DeviceHydroBatchBlock> host_blocks,
    const DeviceHydroBatchBlock* device_blocks, ARCH_CUDA_HYDRO_EOS_TYPE eos,
    double coefficient, double density_floor, double min_e, double max_e,
    const scheduler::StageDescriptor& descriptor, double dt, cudaStream_t stream,
    SpeciesWorkspaceView workspace, Physical::Gravity::ExternalGravityView gravity)
{
    CudaBackendLaunchResult result{cudaErrorInvalidValue, 0, false};
    HydroBatchLaunchVisitor<ARCH_CUDA_HYDRO_EOS_TYPE> visitor{host_blocks,
        device_blocks, eos, coefficient, density_floor, min_e, max_e,
        descriptor, dt, stream, workspace, gravity, result};
    const bool found = visit_cuda_hydro_route(plan, visitor);
    result.route_found = found;
    return result;
}

#undef ARCH_DEFINE_BACKEND_HYDRO

} // namespace arch::cuda

#undef ARCH_CUDA_HYDRO_EOS_TYPE
