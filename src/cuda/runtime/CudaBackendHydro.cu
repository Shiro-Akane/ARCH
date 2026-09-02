#include "CudaBackendHydro.h"

#include "cuda/hydro/HydroIntegratorPolicies.cuh"

namespace arch::cuda {
namespace {

template <class Eos>
cudaError_t launch_hydro_dt_impl(
    DeviceStateView state, DeviceGridView grid, Eos eos, double cfl,
    CudaHydroWorkspaceView workspace, cudaStream_t stream)
{
    return launch_compute_hydro_dt(
        state, grid, eos, cfl, workspace, stream);
}

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
    cudaStream_t stream)
{
    CudaBackendLaunchResult result{cudaErrorInvalidValue, 0, false};
    result.route_found = visit_cuda_hydro_route(
        plan, [&]<class Reconstruction, class Flux> {
            result.error = launch_bounded_hydro_stage<Reconstruction, Flux>(
                old_state, input, output, delta, face_flux, grid, eos,
                entropy_fix_coefficient, density_floor,
                minimum_internal_energy,
                maximum_internal_energy, amr_routes, descriptor, dt, stream,
                result.kernels_launched);
        });
    return result;
}

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
        cudaStream_t stream) \
    { \
        return launch_hydro_stage_impl( \
            plan, old_state, input, output, delta, face_flux, grid, eos, \
            entropy_fix_coefficient, density_floor, minimum_internal_energy, \
            maximum_internal_energy, \
            amr_routes, descriptor, dt, stream); \
    }

ARCH_DEFINE_BACKEND_HYDRO(IdealGasView)
ARCH_DEFINE_BACKEND_HYDRO(HelmEosView)
ARCH_DEFINE_BACKEND_HYDRO(Tabular3DEOSView)
ARCH_DEFINE_BACKEND_HYDRO(Tabular4DEOSView)

#undef ARCH_DEFINE_BACKEND_HYDRO

} // namespace arch::cuda
