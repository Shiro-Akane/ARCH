/** Internal link seam for one compact network x Ideal/Helm EOS burn owner.
 * Uses the common Host-only launch request. These declarations let
 * network selection compile without instantiating any network or ODE kernels.
 */
#pragma once

#include "cuda/runtime/burn/CudaBackendBurn.h"
#include "driver/dispatch/PolicyDescriptor.h"

namespace arch::cuda::burn_detail {

template<class Binding, class Eos>
cudaError_t launch_dense_burn_network_route(
    const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state,
    DeviceGridView grid, std::byte* workspace_storage,
    reduction::ReductionCandidate* candidates, int* statuses,
    DeviceBurnSummary* summary, double burn_dt, Eos eos,
    CudaBurnArguments config, cudaStream_t stream);

// The generated definitions use the same device-capable policy registry. A
// large custom network still receives a defined, explicit Dense rejection stub;
// it does not instantiate the compact BurnPolicyCell or Dense workspace.
#define ARCH_DECLARE_DENSE_BURN_EOS(BINDING, EOS) \
    template<> cudaError_t launch_dense_burn_network_route<BINDING, EOS>( \
        const dispatch::ResolvedExecutionPlan&, DeviceStateView, DeviceGridView, \
        std::byte*, reduction::ReductionCandidate*, int*, DeviceBurnSummary*, \
        double, EOS, CudaBurnArguments, cudaStream_t)
#define ARCH_DECLARE_DENSE_BURN_NETWORK(BINDING) \
    ARCH_DECLARE_DENSE_BURN_EOS(BINDING, IdealGasView); \
    ARCH_DECLARE_DENSE_BURN_EOS(BINDING, HelmEosView)

ARCH_DECLARE_DENSE_BURN_NETWORK(dispatch::CudaAprox13Binding);
ARCH_DECLARE_DENSE_BURN_NETWORK(dispatch::CudaAprox19Binding);
ARCH_DECLARE_DENSE_BURN_NETWORK(dispatch::CudaAprox21Binding);
ARCH_DECLARE_DENSE_BURN_NETWORK(dispatch::CudaIso7Binding);
#define ARCH_DECLARE_DENSE_CUSTOM_NETWORK(TAG, VALUE, NAME, TYPE) \
    ARCH_DECLARE_DENSE_BURN_NETWORK(dispatch::Cuda##TAG##Binding);
ARCH_FOR_EACH_CUDA_CUSTOM_NETWORK(ARCH_DECLARE_DENSE_CUSTOM_NETWORK)
#undef ARCH_DECLARE_DENSE_CUSTOM_NETWORK
#undef ARCH_DECLARE_DENSE_BURN_NETWORK
#undef ARCH_DECLARE_DENSE_BURN_EOS

} // namespace arch::cuda::burn_detail
