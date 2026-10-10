/**
 * @file CudaBackendHydroInstantiation.cuh
 * @brief Instantiate common CUDA hydro launch routes for one EOS type.
 *
 * Registered reconstruction and flux visitors bind the shared numerical
 * policies to device kernels. Views, workspaces and stream are borrowed from
 * the runtime, which checks completion and publishes slot/ghost validity.
 * Workflow:
 * 1. Compile each canonical EOS owner with one target-private flux family.
 * 2. Keep public stage and dt entries in the Vl/Sw/Roe family owner.
 * 3. Select the Hll/Hllc host helper before visiting numerical policies.
 */

#pragma once

#include "cuda/runtime/hydro/CudaBackendHydro.h"

#include "cuda/hydro/policies/CheckedHydroEos.cuh"
#include "cuda/hydro/policies/HydroIntegratorPolicies.cuh"
#include "cuda/hydro/kernels/HydroBatchKernels.cuh"

#ifndef ARCH_CUDA_HYDRO_EOS_TYPE
#error "a CUDA Hydro EOS owner must define ARCH_CUDA_HYDRO_EOS_TYPE"
#endif

#if defined(ARCH_CUDA_HYDRO_FLUX_FAMILY1) == defined(ARCH_CUDA_HYDRO_FLUX_FAMILY2)
#error "a CUDA Hydro EOS owner must select exactly one target-private flux family"
#endif

namespace arch::cuda {
namespace {

#if defined(ARCH_CUDA_HYDRO_FLUX_FAMILY1)
template <class Eos>
cudaError_t launch_hydro_dt_impl(
    DeviceStateView state, DeviceGridView grid, Eos eos, double cfl,
    CudaHydroWorkspaceView workspace, cudaStream_t stream)
{
    return launch_compute_hydro_dt(
        state, grid, make_checked_hydro_eos(eos, workspace.cfl_status),
        cfl, workspace, stream);
}
#endif

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

template <int FluxFamily, class Eos>
CudaBackendLaunchResult launch_hydro_stage_batch_family_impl(
    const dispatch::ResolvedExecutionPlan& plan,
    std::span<const DeviceHydroBatchBlock> host_blocks,
    const DeviceHydroBatchBlock* device_blocks, Eos eos,
    double coefficient, double density_floor, double min_e, double max_e,
    const scheduler::StageDescriptor& descriptor, double dt, cudaStream_t stream,
    SpeciesWorkspaceView workspace, Physical::Gravity::ExternalGravityView gravity)
{
    CudaBackendLaunchResult result{cudaErrorInvalidValue, 0, false};
    HydroBatchLaunchVisitor<Eos> visitor{host_blocks,
        device_blocks, eos, coefficient, density_floor, min_e, max_e,
        descriptor, dt, stream, workspace, gravity, result};
    const bool found = visit_cuda_hydro_route<FluxFamily>(plan, visitor);
    result.route_found = found;
    return result;
}

} // namespace

namespace detail {

// Host-only cross-owner entry; each family's device calls stay in its own TU.
CudaBackendLaunchResult launch_cuda_backend_hydro_stage_batch_hll(
    const dispatch::ResolvedExecutionPlan& plan,
    std::span<const DeviceHydroBatchBlock> host_blocks,
    const DeviceHydroBatchBlock* device_blocks, ARCH_CUDA_HYDRO_EOS_TYPE eos,
    double coefficient, double density_floor, double min_e, double max_e,
    const scheduler::StageDescriptor& descriptor, double dt, cudaStream_t stream,
    SpeciesWorkspaceView workspace, Physical::Gravity::ExternalGravityView gravity);

#if defined(ARCH_CUDA_HYDRO_FLUX_FAMILY2)
CudaBackendLaunchResult launch_cuda_backend_hydro_stage_batch_hll(
    const dispatch::ResolvedExecutionPlan& plan,
    std::span<const DeviceHydroBatchBlock> host_blocks,
    const DeviceHydroBatchBlock* device_blocks, ARCH_CUDA_HYDRO_EOS_TYPE eos,
    double coefficient, double density_floor, double min_e, double max_e,
    const scheduler::StageDescriptor& descriptor, double dt, cudaStream_t stream,
    SpeciesWorkspaceView workspace, Physical::Gravity::ExternalGravityView gravity)
{
    return launch_hydro_stage_batch_family_impl<2>(plan, host_blocks,
        device_blocks, eos, coefficient, density_floor, min_e, max_e,
        descriptor, dt, stream, workspace, gravity);
}
#endif

} // namespace detail

#if defined(ARCH_CUDA_HYDRO_FLUX_FAMILY1)
#define ARCH_DEFINE_BACKEND_HYDRO(EOS) \
    CudaBackendLaunchResult launch_cuda_backend_hydro_dt_batch( \
        std::span<const DeviceHydroDtBatchBlock> host, \
        const DeviceHydroDtBatchBlock* device, EOS eos, double cfl, \
        SpeciesWorkspaceView workspace, cudaStream_t stream) \
    { return launch_hydro_dt_batch(host, device, eos, cfl, workspace, stream); } \
    cudaError_t launch_cuda_backend_hydro_dt( \
        DeviceStateView state, DeviceGridView grid, EOS eos, double cfl, \
        CudaHydroWorkspaceView workspace, cudaStream_t stream) \
    { \
        return launch_hydro_dt_impl( \
            state, grid, eos, cfl, workspace, stream); \
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
    if (plan.flux == dispatch::FluxId::Hll || plan.flux == dispatch::FluxId::Hllc)
        return detail::launch_cuda_backend_hydro_stage_batch_hll(plan, host_blocks,
            device_blocks, eos, coefficient, density_floor, min_e, max_e,
            descriptor, dt, stream, workspace, gravity);
    return launch_hydro_stage_batch_family_impl<1>(plan, host_blocks,
        device_blocks, eos, coefficient, density_floor, min_e, max_e,
        descriptor, dt, stream, workspace, gravity);
}

#undef ARCH_DEFINE_BACKEND_HYDRO
#endif

} // namespace arch::cuda

#undef ARCH_CUDA_HYDRO_EOS_TYPE
