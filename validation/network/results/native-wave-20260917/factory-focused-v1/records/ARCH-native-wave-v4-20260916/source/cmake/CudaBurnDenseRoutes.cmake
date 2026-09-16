# Functional compilation boundaries for compact Ideal/Helm burn routes. There
# is one shared kernel/ODE implementation, instantiated per network x EOS.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/CudaBurnNetworks.cmake")

function(arch_register_cuda_dense_burn_routes)
    if(NOT TARGET arch_cuda_backend
       OR NOT TARGET arch_cuda_backend_burn_helm
       OR NOT TARGET arch_cuda_backend_burn_tabular3d
       OR NOT COMMAND arch_configure_cuda_backend_object)
        message(FATAL_ERROR "Dense CUDA routes require the configured backend and thin EOS dispatchers")
    endif()
    if(TARGET arch_cuda_backend_burn_ideal_aprox13)
        message(FATAL_ERROR "Dense CUDA network/EOS routes were already registered")
    endif()

    set(route_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/cuda_dense_burn")
    file(MAKE_DIRECTORY "${route_dir}")
    arch_cuda_burn_network_inventory(network_names binding_names network_headers network_types)

    set(route_targets)
    set(previous_route arch_cuda_backend_burn_helm)
    list(LENGTH network_names network_count)
    math(EXPR last_network "${network_count} - 1")
    foreach(index RANGE 0 ${last_network})
        list(GET network_names ${index} network_name)
        list(GET binding_names ${index} ARCH_DENSE_BINDING)
        list(GET network_headers ${index} ARCH_DENSE_NETWORK_HEADER)
        list(GET network_types ${index} ARCH_DENSE_NETWORK_TYPE)
        foreach(eos_token IN ITEMS ideal helm)
            if(eos_token STREQUAL "ideal")
                set(ARCH_DENSE_EOS IdealGasView)
                set(ARCH_DENSE_EOS_HEADER "physics/eos/IdealGas.h")
            else()
                set(ARCH_DENSE_EOS HelmEosView)
                set(ARCH_DENSE_EOS_HEADER "physics/eos/HelmEos.h")
            endif()
            set(route_target "arch_cuda_backend_burn_${eos_token}_${network_name}")
            set(route_source "${route_dir}/${eos_token}_${network_name}.cu")
            configure_file(
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/CudaBurnDenseRoute.cu.in"
                "${route_source}" @ONLY)
            arch_configure_cuda_backend_object(${route_target} "${route_source}")
            target_link_libraries(${route_target} PRIVATE arch_build_contract CUDA::cudart)
            target_sources(arch_cuda_backend PRIVATE $<TARGET_OBJECTS:${route_target}>)
            # Keep each route in the common heavy compile pool. Dependencies
            # also preserve phase ordering for generators without Ninja pools.
            add_dependencies(${route_target} ${previous_route})
            set(previous_route ${route_target})
            list(APPEND route_targets ${route_target})
        endforeach()
    endforeach()
    # Compile the thin Ideal/Helm dispatchers before their network/EOS objects,
    # then allow the Tabular phase to begin.
    add_dependencies(arch_cuda_backend_burn_tabular3d ${previous_route})
    set(ARCH_CUDA_DENSE_BURN_ROUTE_OBJECT_TARGETS "${route_targets}" PARENT_SCOPE)
    set(ARCH_CUDA_DENSE_BURN_LAST_TARGET "${previous_route}" PARENT_SCOPE)
endfunction()
