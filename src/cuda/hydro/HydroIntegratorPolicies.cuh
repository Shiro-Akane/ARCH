/**
 * @file HydroIntegratorPolicies.cuh
 * @brief Bounded CUDA execution of one E1 Hydro stage descriptor.
 */

#pragma once

#include "HydroStageKernels.cuh"
#include "driver/dispatch/PolicyDescriptor.h"

#include <type_traits>

namespace arch::cuda {

template <class Binding> struct CudaLimiterType;
template <> struct CudaLimiterType<dispatch::CudaMinModBinding> {
    using type = MinMod;
};
template <> struct CudaLimiterType<dispatch::CudaMcBinding> {
    using type = McLimiter;
};
template <> struct CudaLimiterType<dispatch::CudaSuperBeeBinding> {
    using type = SuperBee;
};
template <> struct CudaLimiterType<dispatch::CudaVanLeerLimiterBinding> {
    using type = VanLeer;
};

template <class Binding> struct CudaReconstructionType;
template <> struct CudaReconstructionType<dispatch::CudaPcmBinding> {
    using type = CudaPcmReconstruction;
};
template <> struct CudaReconstructionType<dispatch::CudaPpmBinding> {
    using type = CudaPpmReconstruction;
};

template <class Binding> struct CudaFluxType;
template <> struct CudaFluxType<dispatch::CudaVlBinding> {
    using type = CudaVlFlux;
};
template <> struct CudaFluxType<dispatch::CudaSwBinding> {
    using type = CudaSwFlux;
};
template <> struct CudaFluxType<dispatch::CudaRoeBinding> {
    using type = CudaRoeFlux;
};
template <> struct CudaFluxType<dispatch::CudaHllBinding> {
    using type = CudaHllFlux;
};
template <> struct CudaFluxType<dispatch::CudaHllcBinding> {
    using type = CudaHllcFlux;
};

template <class Function>
bool visit_cuda_hydro_route(
    const dispatch::ResolvedExecutionPlan& plan, Function&& function)
{
    bool invoked = false;
    const bool flux_found = dispatch::visit_policy<dispatch::FluxPolicies>(
        plan.flux, [&]<class FluxRegistration> {
            using Flux = typename CudaFluxType<
                typename dispatch::PolicyRegistration<
                    FluxRegistration>::CudaBinding>::type;
            const bool reconstruction_found =
                dispatch::visit_policy<dispatch::ReconstructionPolicies>(
                    plan.reconstruction,
                    [&]<class ReconstructionRegistration> {
                        using Binding = typename dispatch::PolicyRegistration<
                            ReconstructionRegistration>::CudaBinding;
                        if constexpr (std::is_same_v<
                                          Binding,
                                          dispatch::CudaMusclBinding>) {
                            const bool limiter_found =
                                dispatch::visit_policy<
                                    dispatch::LimiterPolicies>(
                                    plan.limiter,
                                    [&]<class LimiterRegistration> {
                                        using Limiter = typename CudaLimiterType<
                                            typename dispatch::PolicyRegistration<
                                                LimiterRegistration>::CudaBinding>::type;
                                        using Reconstruction =
                                            CudaMusclReconstruction<Limiter>;
                                        function.template operator()<
                                            Reconstruction, Flux>();
                                        invoked = true;
                                    });
                            invoked = invoked && limiter_found;
                        } else {
                            using Reconstruction =
                                typename CudaReconstructionType<Binding>::type;
                            function.template operator()<Reconstruction, Flux>();
                            invoked = true;
                        }
                    });
            invoked = invoked && reconstruction_found;
        });
    return flux_found && invoked;
}

template <class Reconstruction, class Flux, class EosView>
cudaError_t launch_bounded_hydro_stage(
    DeviceStateView old_state, DeviceStateView input,
    DeviceStateView output, DeviceStateView delta,
    DeviceStateView face_flux, DeviceGridView grid,
    const EosView& eos, double entropy_fix_coefficient,
    double density_floor, double minimum_internal_energy,
    double maximum_internal_energy,
    const CudaAmrFluxDirectionRouteView* amr_routes,
    const scheduler::StageDescriptor& descriptor, double dt,
    cudaStream_t stream, int& kernels_launched)
{
    kernels_launched = 0;
    cudaError_t error = clear_hydro_buffer(delta, stream);
    if (error != cudaSuccess) return error;
    error = cudaMemsetAsync(
        output.enuc_rate, 0,
        static_cast<std::size_t>(output.total_size) * sizeof(double), stream);
    if (error != cudaSuccess) return error;
    for (int direction = 0; direction < 3; ++direction) {
        if (direction >= grid.dim) break;
        error = launch_hydro_faces<Reconstruction, Flux>(
            input, face_flux, grid, eos, direction,
            entropy_fix_coefficient, stream);
        if (error != cudaSuccess) return error;
        ++kernels_launched;
        error = launch_hydro_divergence(
            face_flux, delta, grid, dt, direction, stream);
        if (error != cudaSuccess) return error;
        ++kernels_launched;
        if (amr_routes != nullptr) {
            const auto registration = launch_cuda_amr_flux_register(
                amr_routes[direction], AmrFluxSource::StageScratch,
                descriptor.flux_register_weight, stream);
            if (registration.error != cudaSuccess)
                return registration.error;
            kernels_launched += registration.kernels_launched;
        }
    }
    error = launch_hydro_single_stage_update(
        old_state, input, output, delta, grid,
        descriptor.old_weight, descriptor.update_weight,
        density_floor, minimum_internal_energy, maximum_internal_energy,
        stream);
    if (error == cudaSuccess) ++kernels_launched;
    return error;
}

} // namespace arch::cuda
