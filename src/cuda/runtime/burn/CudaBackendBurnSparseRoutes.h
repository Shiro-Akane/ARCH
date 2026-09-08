/** Internal link seam: one sparse owner instantiation per network/EOS TU. */
#pragma once

#include "cuda/runtime/burn/CudaBackendBurnSparse.h"

namespace arch::cuda::burn_detail {

template<class Binding, class Eos>
std::unique_ptr<CudaSparseBurnOwner> make_sparse_burn_owner_route(
    const dispatch::ResolvedExecutionPlan& plan, Eos eos,
    int max_cells, cudaStream_t stream);

// Declarations must precede the lightweight visitor's uses. The definitions
// are generated from this same registry, never from network physics copies.
#define ARCH_DECLARE_SPARSE_OWNER_EOS(BINDING, EOS) \
    template<> std::unique_ptr<CudaSparseBurnOwner> \
    make_sparse_burn_owner_route<BINDING, EOS>( \
        const dispatch::ResolvedExecutionPlan&, EOS, int, cudaStream_t)
#define ARCH_DECLARE_SPARSE_OWNER_NETWORK(BINDING) \
    ARCH_DECLARE_SPARSE_OWNER_EOS(BINDING, IdealGasView); \
    ARCH_DECLARE_SPARSE_OWNER_EOS(BINDING, HelmEosView); \
    ARCH_DECLARE_SPARSE_OWNER_EOS(BINDING, Tabular3DEOSView); \
    ARCH_DECLARE_SPARSE_OWNER_EOS(BINDING, Tabular4DEOSView)

ARCH_DECLARE_SPARSE_OWNER_NETWORK(dispatch::CudaAprox13Binding);
ARCH_DECLARE_SPARSE_OWNER_NETWORK(dispatch::CudaAprox19Binding);
ARCH_DECLARE_SPARSE_OWNER_NETWORK(dispatch::CudaAprox21Binding);
ARCH_DECLARE_SPARSE_OWNER_NETWORK(dispatch::CudaIso7Binding);
#define ARCH_DECLARE_SPARSE_CUSTOM_OWNER(TAG, VALUE, NAME, TYPE) \
    ARCH_DECLARE_SPARSE_OWNER_NETWORK(dispatch::Cuda##TAG##Binding);
ARCH_FOR_EACH_CUDA_CUSTOM_NETWORK(ARCH_DECLARE_SPARSE_CUSTOM_OWNER)
#undef ARCH_DECLARE_SPARSE_CUSTOM_OWNER
#undef ARCH_DECLARE_SPARSE_OWNER_NETWORK
#undef ARCH_DECLARE_SPARSE_OWNER_EOS

} // namespace arch::cuda::burn_detail
