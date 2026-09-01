/**
 * @file CudaBurnNetworkTypes.h
 * @brief Single CUDA binding-to-network type map for burn runtime dispatch.
 */

#pragma once

#include "numerics/burnsolver/Networks.h"

namespace arch::cuda::burn_detail {

template <class Binding>
struct NetworkType;

template <>
struct NetworkType<dispatch::CudaAprox13Binding> {
    using type = NetAprox13;
};

template <>
struct NetworkType<dispatch::CudaAprox19Binding> {
    using type = NetAprox19;
};

template <>
struct NetworkType<dispatch::CudaAprox21Binding> {
    using type = NetAprox21;
};

template <>
struct NetworkType<dispatch::CudaIso7Binding> {
    using type = NetIso7;
};

#define ARCH_BIND_CUDA_CUSTOM_NETWORK(TAG, VALUE, NAME, TYPE) \
    template <> struct NetworkType<dispatch::Cuda##TAG##Binding> { \
        using type = TYPE; \
    };
ARCH_FOR_EACH_CUDA_CUSTOM_NETWORK(ARCH_BIND_CUDA_CUSTOM_NETWORK)
#undef ARCH_BIND_CUDA_CUSTOM_NETWORK

template <class Binding>
using NetworkTypeFor = typename NetworkType<Binding>::type;

} // namespace arch::cuda::burn_detail
