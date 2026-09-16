/**
 * @file Networks.h
 * @brief Compile-time network include surface for static duck typing.
 */
#pragma once

// Linear algebra backends shared by all networks.
#include "../linalg/DenseWrap.h"
#include "../linalg/SparseWrap.h"

// Nuclear reaction-network policies.
#include "../../physics/network/aprox13/NetAprox13.h"
#include "../../physics/network/aprox19/NetAprox19.h"
#include "../../physics/network/aprox21/NetAprox21.h"
#include "../../physics/network/iso7/NetIso7.h"

// Generated at CMake configure time from custom/*/network.cmake packages.
#include "CustomNetworks.generated.h"

#include "../../driver/dispatch/PolicyDescriptor.h"

template <class Binding>
struct CpuNetworkType;

template <> struct CpuNetworkType<arch::dispatch::CpuAprox13Binding> { using type = NetAprox13; };
template <> struct CpuNetworkType<arch::dispatch::CpuAprox19Binding> { using type = NetAprox19; };
template <> struct CpuNetworkType<arch::dispatch::CpuAprox21Binding> { using type = NetAprox21; };
template <> struct CpuNetworkType<arch::dispatch::CpuIso7Binding> { using type = NetIso7; };

#define ARCH_BIND_CPU_NETWORK_TYPE(TAG, VALUE, NAME, TYPE) \
    template <> struct CpuNetworkType< \
        arch::dispatch::Cpu##TAG##Binding> { using type = TYPE; };
ARCH_FOR_EACH_CUSTOM_NETWORK(ARCH_BIND_CPU_NETWORK_TYPE)
#undef ARCH_BIND_CPU_NETWORK_TYPE

// NetType requirements are enforced by template instantiation.
