/**
 * @file HydroIntegratorPolicies.cuh
 * @brief Registered CUDA execution of a shared hydro stage descriptor.
 *
 * Registry visitors select shared reconstruction, limiter and flux policies.
 * Launches accumulate directional fluxes and sources into borrowed stage
 * buffers; runtime control owns ghost readiness, completion and slot rotation.
 */

#pragma once

#include "HydroStageKernels.cuh"
#include "HydroSourceKernels.cuh"
#include "cuda/runtime/amr/CudaBackendAmrFlux.h"
#include "driver/StageScheduler.h"
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

namespace detail {

// NVCC 12.3 can corrupt template-parameter names when lowering nested explicit-
// template lambdas to Host C++. Named visitors express the same registry walk
// without relying on nested closure templates; they contain no physics policy.
template <class Function, class Flux>
struct HydroLimiterVisitor {
    Function& function;
    bool& invoked;

    template <class Registration>
    void operator()()
    {
        using Limiter = typename CudaLimiterType<
            typename dispatch::PolicyRegistration<Registration>::CudaBinding>::type;
        function.template operator()<CudaMusclReconstruction<Limiter>, Flux>();
        invoked = true;
    }
};

template <class Function, class Flux>
struct HydroReconstructionVisitor {
    const dispatch::ResolvedExecutionPlan& plan;
    Function& function;
    bool& invoked;

    template <class Registration>
    void operator()()
    {
        using Binding = typename dispatch::PolicyRegistration<Registration>::CudaBinding;
        if constexpr (std::is_same_v<Binding, dispatch::CudaMusclBinding>) {
            HydroLimiterVisitor<Function, Flux> visitor{function, invoked};
            const bool found = dispatch::visit_policy<dispatch::LimiterPolicies>(plan.limiter, visitor);
            invoked = invoked && found;
        } else {
            using Reconstruction = typename CudaReconstructionType<Binding>::type;
            function.template operator()<Reconstruction, Flux>();
            invoked = true;
        }
    }
};

template <class Function>
struct HydroFluxVisitor {
    const dispatch::ResolvedExecutionPlan& plan;
    Function& function;
    bool& invoked;

    template <class Registration>
    void operator()()
    {
        using Flux = typename CudaFluxType<
            typename dispatch::PolicyRegistration<Registration>::CudaBinding>::type;
        HydroReconstructionVisitor<Function, Flux> visitor{plan, function, invoked};
        const bool found = dispatch::visit_policy<dispatch::ReconstructionPolicies>(plan.reconstruction, visitor);
        invoked = invoked && found;
    }
};

} // namespace detail

template <class Function>
bool visit_cuda_hydro_route(
    const dispatch::ResolvedExecutionPlan& plan, Function&& function)
{
    bool invoked = false;
    detail::HydroFluxVisitor<std::remove_reference_t<Function>> visitor{plan, function, invoked};
    const bool found = dispatch::visit_policy<dispatch::FluxPolicies>(plan.flux, visitor);
    return found && invoked;
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
    cudaStream_t stream, int& kernels_launched,
    SpeciesWorkspaceView species_workspace = {}, Physical::Gravity::ExternalGravityView gravity = {})
{
    kernels_launched = 0;
    if (!valid_species_workspace(species_workspace, input.n_species, 4))
        return cudaErrorInvalidValue;
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
            entropy_fix_coefficient, stream, species_workspace);
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
    if (grid.geometry != static_cast<int>(DeviceGeometry::Cartesian) || gravity.enabled) {
        error = launch_hydro_sources(input, delta, grid, eos, dt, stream,
                                     species_workspace, gravity);
        if (error != cudaSuccess) return error;
        ++kernels_launched;
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
