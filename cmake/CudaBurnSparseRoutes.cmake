# Sparse production bindings, intentionally separate from numerical code.
# Call after arch_cuda_backend and the two arch_configure_cuda_*_object helpers
# exist. Custom IDs must be the same device-capable IDs used to emit the public
# ARCH_FOR_EACH_CUDA_CUSTOM_NETWORK registry; CPU-only packages are not included.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/CudaBurnNetworks.cmake")

function(arch_register_cuda_sparse_burn_routes)
    if(NOT TARGET arch_cuda_backend
       OR NOT COMMAND arch_configure_cuda_backend_object
       OR NOT COMMAND arch_configure_cuda_host_object)
        message(FATAL_ERROR "Sparse CUDA routes require the configured backend object helpers")
    endif()
    if(TARGET arch_cuda_backend_sparse_factory)
        message(FATAL_ERROR "Sparse CUDA production routes were already registered")
    endif()

    # The factory is always linked: a CUDA build without cuDSS still has a
    # defined, explicit rejection path and no dangling public ABI symbols.
    arch_configure_cuda_host_object(arch_cuda_backend_sparse_factory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/runtime/burn/CudaBackendBurnSparseFactory.cpp" -g1)
    target_sources(arch_cuda_backend PRIVATE
        $<TARGET_OBJECTS:arch_cuda_backend_sparse_factory>)
    set(route_targets arch_cuda_backend_sparse_factory)
    set(previous_route arch_cuda_backend_sparse_factory)

    if(TARGET arch_cuda_sparse_provider)
        set(route_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/cuda_sparse_burn")
        file(MAKE_DIRECTORY "${route_dir}")
        arch_cuda_burn_network_inventory(network_names binding_names network_headers network_types)
        list(LENGTH network_names network_count)
        math(EXPR last_network "${network_count} - 1")
        foreach(index RANGE 0 ${last_network})
            list(GET network_names ${index} network_name)
            list(GET binding_names ${index} ARCH_SPARSE_BINDING)
            list(GET network_headers ${index} ARCH_SPARSE_NETWORK_HEADER)
            list(GET network_types ${index} ARCH_SPARSE_NETWORK_TYPE)
            foreach(eos_name IN ITEMS IdealGasView HelmEosView Tabular3DEOSView Tabular4DEOSView)
                set(ARCH_SPARSE_EOS "${eos_name}")
                string(REGEX REPLACE "View$" "" eos_header_name "${eos_name}")
                set(ARCH_SPARSE_EOS_HEADER "physics/eos/${eos_header_name}.h")
                string(TOLOWER "${eos_name}" eos_token)
                set(route_target "arch_cuda_backend_sparse_${network_name}_${eos_token}")
                set(route_source "${route_dir}/${network_name}_${eos_token}.cu")
                configure_file(
                    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/CudaBurnSparseOwner.cu.in"
                    "${route_source}" @ONLY)
                arch_configure_cuda_backend_object(${route_target} "${route_source}")
                target_link_libraries(${route_target} PRIVATE
                    arch_build_contract arch_cuda_sparse_provider)
                target_sources(arch_cuda_backend PRIVATE $<TARGET_OBJECTS:${route_target}>)
                # The shared Ninja pool imposes the hard memory bound. This
                # dependency also preserves the functional route sequence for
                # Makefile generators, without adding a new compile policy.
                add_dependencies(${route_target} ${previous_route})
                set(previous_route ${route_target})
                list(APPEND route_targets ${route_target})
            endforeach()
        endforeach()
        # Capability is published only together with a complete production
        # factory matrix AND the actual provider link, never on discovery alone.
        target_link_libraries(arch_cuda_backend PRIVATE arch_cuda_sparse_provider)
        target_compile_definitions(arch_build_contract INTERFACE ARCH_HAS_CUDSS_PROVIDER=1)
    endif()

    set(ARCH_CUDA_SPARSE_ROUTE_OBJECT_TARGETS "${route_targets}" PARENT_SCOPE)
    set(ARCH_CUDA_SPARSE_LAST_TARGET "${previous_route}" PARENT_SCOPE)
endfunction()
