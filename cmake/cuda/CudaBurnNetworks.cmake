# Compilation inventory shared by dense and sparse delegate generation.
# Runtime policy selection remains in C++ NetworkPolicies; this list never
# selects a network or changes a physical model at runtime.
include_guard(GLOBAL)

function(arch_cuda_burn_network_inventory output_names output_bindings output_headers output_types)
    set(names aprox13 aprox19 aprox21 iso7)
    set(bindings CudaAprox13Binding CudaAprox19Binding CudaAprox21Binding CudaIso7Binding)
    set(headers physics/network/aprox13/NetAprox13.h physics/network/aprox19/NetAprox19.h
        physics/network/aprox21/NetAprox21.h physics/network/iso7/NetIso7.h)
    set(types NetAprox13 NetAprox19 NetAprox21 NetIso7)
    foreach(custom_id IN LISTS ARCH_CUSTOM_CUDA_IDS)
        if(NOT custom_id MATCHES "^[a-z][a-z0-9_]*$")
            message(FATAL_ERROR "Invalid device custom-network ID: ${custom_id}")
        endif()
        list(APPEND names "custom_${custom_id}")
        list(APPEND bindings "CudaCustom_${custom_id}Binding")
        list(APPEND headers "${ARCH_CUSTOM_HEADER_${custom_id}}")
        list(APPEND types "${ARCH_CUSTOM_TYPE_${custom_id}}")
    endforeach()
    set(${output_names} "${names}" PARENT_SCOPE)
    set(${output_bindings} "${bindings}" PARENT_SCOPE)
    set(${output_headers} "${headers}" PARENT_SCOPE)
    set(${output_types} "${types}" PARENT_SCOPE)
endfunction()
