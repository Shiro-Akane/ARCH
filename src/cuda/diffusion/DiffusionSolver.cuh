/**
 * @file DiffusionSolver.cuh
 * @brief Bounded physical operations for one E1 RKL callback.
 */

#pragma once

#include "DiffusionKernels.cuh"

namespace arch::cuda {

inline cudaError_t copy_diffusion_slot(
    DeviceStateView source, DeviceStateView destination,
    cudaStream_t stream)
{
    if (!valid_hydro_view(source) || !valid_hydro_view(destination)
        || !detail::matching_state_shape(source, destination)
        || detail::any_state_storage_alias(destination, source))
        return cudaErrorInvalidValue;
    const std::size_t bytes =
        static_cast<std::size_t>(source.total_size) * sizeof(double);
    cudaError_t error = cudaSuccess;
    const double* sources[] = {
        source.rho, source.mom_u, source.mom_v, source.mom_w,
        source.eng, source.enuc_rate};
    double* destinations[] = {
        destination.rho, destination.mom_u, destination.mom_v,
        destination.mom_w, destination.eng, destination.enuc_rate};
    for (int field = 0; field < 6 && error == cudaSuccess; ++field) {
        error = cudaMemcpyAsync(destinations[field], sources[field], bytes,
                                cudaMemcpyDeviceToDevice, stream);
    }
    if (error == cudaSuccess && source.n_species > 0) {
        error = cudaMemcpyAsync(
            destination.mass_fractions, source.mass_fractions,
            static_cast<std::size_t>(source.n_species) * bytes,
            cudaMemcpyDeviceToDevice, stream);
    }
    return error;
}

template <class EosView>
DiffusionLaunchResult launch_bounded_diffusion_operator(
    DeviceStateView source, DeviceStateView increment,
    const EosView& eos, SpeciesPODView species, DeviceGridView grid,
    DiffFlux::DiffusionConfigView config, DiffusionWorkspaceView workspace,
    const CudaAmrFluxDirectionRouteView* amr_routes,
    double registration_weight, bool capture_initial_operator,
    cudaStream_t stream)
{
    return launch_diffusion_operator(
        source, increment, eos, species, grid, config, workspace,
        amr_routes, registration_weight, capture_initial_operator, stream);
}

inline DiffusionLaunchResult launch_bounded_first_rkl_stage(
    DeviceStateView state_n, DeviceStateView increment,
    DeviceStateView destination, DeviceGridView grid,
    DiffFlux::DiffusionConfigView config, double coefficient,
    cudaStream_t stream)
{
    return launch_first_rkl_stage(
        state_n, increment, destination, grid, config, coefficient, stream);
}

inline DiffusionLaunchResult launch_bounded_recursive_rkl_stage(
    DeviceStateView state_n, DeviceStateView previous,
    DeviceStateView older, DeviceStateView increment_previous,
    DeviceStateView increment_initial, DeviceStateView destination,
    DeviceGridView grid, DiffFlux::DiffusionConfigView config,
    DiffFunction::RKLCoeffs coefficients, bool second_order,
    double dt, bool increments_are_scaled, cudaStream_t stream)
{
    return launch_recursive_rkl_stage(
        state_n, previous, older, increment_previous, increment_initial,
        destination, grid, config, coefficients, second_order, dt,
        increments_are_scaled, stream);
}

} // namespace arch::cuda
