# CUDA dependency discovery, canonical object owners and bounded build phases.
# Included only when ARCH_ENABLE_CUDA is enabled; focused tests live separately.

enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)
include(cmake/CudaCodeImages.cmake)
arch_resolve_cuda_code_images(CMAKE_CUDA_ARCHITECTURES)
string(JOIN "," arch_cuda_code_images ${CMAKE_CUDA_ARCHITECTURES})
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" arch_cuda_compiler_version "${CMAKE_CUDA_COMPILER_VERSION}")
math(EXPR arch_cuda_ptx_version "${CMAKE_MATCH_1} * 1000 + ${CMAKE_MATCH_2} * 10")
# Source-private metadata: changing image targets must not introduce CUDA
# headers or another compile definition into the common physics templates.
set_property(SOURCE src/driver/dispatch/RuntimeProbe.cpp APPEND PROPERTY
    COMPILE_DEFINITIONS "ARCH_CUDA_CODE_IMAGES=\"${arch_cuda_code_images}\""
                        "ARCH_CUDA_PTX_VERSION=${arch_cuda_ptx_version}")
message(STATUS "[CUDA] Compiled code images: ${CMAKE_CUDA_ARCHITECTURES}")
if(ARCH_ENABLE_CUDSS)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
    find_package(CuDSS QUIET)
    if(CuDSS_FOUND)
        message(STATUS "[DEP] cuDSS found: ${CuDSS_LIBRARY}")
        # Backend numerical refinement, not an ODE tolerance or network-size
        # switch. Keep it private to the Host provider so tuning it does not
        # reinstantiate the CUDA network/EOS matrix.
        set(ARCH_CUDSS_IR_STEPS "2" CACHE STRING
            "cuDSS correction passes before ARCH's original-matrix residual check (0 disables)")
        string(LENGTH "${ARCH_CUDSS_IR_STEPS}" arch_cudss_ir_length)
        if(NOT ARCH_CUDSS_IR_STEPS MATCHES "^(0|[1-9][0-9]*)$"
           OR arch_cudss_ir_length GREATER 10
           OR ARCH_CUDSS_IR_STEPS GREATER 2147483647)
            message(FATAL_ERROR "ARCH_CUDSS_IR_STEPS must be a nonnegative cuDSS int")
        endif()
        add_library(arch_cuda_sparse_provider STATIC
            src/cuda/microphysics/CuDssSparseSolver.cpp
            src/cuda/microphysics/SparseEquilibration.cu)
        target_compile_definitions(arch_cuda_sparse_provider PRIVATE
            ARCH_CUDSS_IR_STEPS=${ARCH_CUDSS_IR_STEPS})
        target_compile_options(arch_cuda_sparse_provider PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
        target_link_libraries(arch_cuda_sparse_provider PUBLIC
            arch_build_contract CuDSS::cudss)
        set_target_properties(arch_cuda_sparse_provider PROPERTIES
            CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON
            INTERPROCEDURAL_OPTIMIZATION OFF)
    else()
        message(STATUS "[DEP] cuDSS not found; CUDA sparse solver unavailable")
    endif()
endif()

# ccache 4.x understands NVCC dependency/output modes.  Apply the same
# launcher used by Host C++ to the split CUDA matrix so a small dispatcher
# or header edit does not force every network/EOS object through NVCC
# again.  This stays CUDA-opt-in because the language is enabled above.
if(CCACHE_PROGRAM)
    set(CMAKE_CUDA_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    if(TARGET arch_cuda_sparse_provider)
        set_property(TARGET arch_cuda_sparse_provider PROPERTY
            CUDA_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    endif()
    message(STATUS "[OPT] CUDA compiler cache enabled: ${CCACHE_PROGRAM}")
endif()

# Ninja may otherwise start every independent object edge even when its
# logical target participates in an add_dependencies chain.  Put all CUDA
# backend frontends (NVCC and the table-heavy Host owners) in one named
# compile pool. Top-level parallelism may use the remaining jobs for the
# lightweight graph, while this lane is bounded by the configured slot
# count (conservative default 1). Slot count is not a RAM guarantee;
# measure the actual workload with the memory guard before raising it.
# Non-Ninja generators retain the target ordering
# below and simply ignore this optional scheduling refinement.
if(CMAKE_GENERATOR MATCHES "Ninja")
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS arch_cuda_heavy=${ARCH_CUDA_HEAVY_COMPILE_JOBS})
    set(ARCH_CUDA_HEAVY_JOB_POOL arch_cuda_heavy)
    if(TARGET arch_cuda_sparse_provider)
        set_property(TARGET arch_cuda_sparse_provider PROPERTY
            JOB_POOL_COMPILE ${ARCH_CUDA_HEAVY_JOB_POOL})
    endif()
endif()

# add_dependencies() alone only creates an object-order phony edge for
# Ninja.  It does not require the dependency archive to exist before the
# dependent target starts compiling.  Turn completion of each heavy phase
# into a file-backed edge so a high top-level --parallel value cannot make
# the CUDA frontend overlap the dispatch template matrix.
function(arch_add_completion_barrier barrier dependency)
    set(stamp
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${barrier}.stamp")
    add_custom_command(
        OUTPUT "${stamp}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
        DEPENDS "${dependency}"
        COMMENT "Completing ${dependency} before the next heavy phase"
        VERBATIM)
    add_custom_target(${barrier} DEPENDS "${stamp}")
endfunction()

# Order adjacent owners; file-backed barriers below still separate full phases.
# The object declarations remain explicit so each source has a visible owner.
function(arch_order_cuda_targets)
    set(previous "")
    foreach(target IN LISTS ARGN)
        if(previous)
            add_dependencies(${target} ${previous})
        endif()
        set(previous "${target}")
    endforeach()
endfunction()

# Put every NVCC frontend in the same bounded lane. Burn/EOS, hydro, and
# diffusion instantiate large device templates, and even the small
# exchange wrapper can push an 8 GiB WSL guest over its limit when it
# overlaps a memory-heavy host-control phase.
# The only public/linkable backend target remains arch_cuda_backend.
function(arch_configure_cuda_backend_object target source)
    add_library(${target} OBJECT ${source})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;-Xcompiler=-march=native>)
    set_target_properties(${target} PROPERTIES
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON
        INTERPROCEDURAL_OPTIMIZATION OFF)
    if(ARCH_CUDA_HEAVY_JOB_POOL)
        set_property(TARGET ${target} PROPERTY JOB_POOL_COMPILE
            ${ARCH_CUDA_HEAVY_JOB_POOL})
    endif()
endfunction()

arch_configure_cuda_backend_object(arch_cuda_backend_burn_ideal
    src/cuda/runtime/burn/routes/CudaBackendBurnIdeal.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_burn_helm
    src/cuda/runtime/burn/routes/CudaBackendBurnHelm.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular3d
    src/cuda/runtime/burn/routes/CudaBackendBurnTabular3D.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular3d_aprox13
    src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DAprox13.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular3d_aprox19
    src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DAprox19.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular3d_aprox21
    src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DAprox21.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular3d_iso7
    src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DIso7.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular4d
    src/cuda/runtime/burn/routes/CudaBackendBurnTabular4D.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular4d_aprox13
    src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DAprox13.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular4d_aprox19
    src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DAprox19.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular4d_aprox21
    src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DAprox21.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular4d_iso7
    src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DIso7.cu)
# Hydro owns one shared template implementation and one canonical source
# owner per EOS.  Heavy tabular interpolation call boundaries keep NVCC
# Debug frontend memory bounded without duplicating physics or routes.
arch_configure_cuda_backend_object(arch_cuda_backend_hydro_ideal
    src/cuda/runtime/hydro/CudaBackendHydroIdeal.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_hydro_helm
    src/cuda/runtime/hydro/CudaBackendHydroHelm.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_hydro_tabular3
    src/cuda/runtime/hydro/CudaBackendHydroTabular3.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_hydro_tabular4
    src/cuda/runtime/hydro/CudaBackendHydroTabular4.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_diffusion
    src/cuda/runtime/diffusion/CudaBackendDiffusion.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_exchange
    src/cuda/runtime/amr/CudaBackendExchange.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_amr_flux
    src/cuda/runtime/amr/CudaBackendAmrFlux.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_amr_indicators
    src/cuda/amr/RefinementIndicators.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_amr_migration
    src/cuda/amr/RegridMigration.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_grid_metrics
    src/cuda/common/GridMetricsCache.cu)
# Retain the documented device-owner order within the bounded compile pool.
arch_order_cuda_targets(
    arch_cuda_backend_burn_ideal
    arch_cuda_backend_burn_helm
    arch_cuda_backend_burn_tabular3d
    arch_cuda_backend_burn_tabular3d_aprox13
    arch_cuda_backend_burn_tabular3d_aprox19
    arch_cuda_backend_burn_tabular3d_aprox21
    arch_cuda_backend_burn_tabular3d_iso7
    arch_cuda_backend_burn_tabular4d
    arch_cuda_backend_burn_tabular4d_aprox13
    arch_cuda_backend_burn_tabular4d_aprox19
    arch_cuda_backend_burn_tabular4d_aprox21
    arch_cuda_backend_burn_tabular4d_iso7
    arch_cuda_backend_hydro_ideal
    arch_cuda_backend_hydro_helm
    arch_cuda_backend_hydro_tabular3
    arch_cuda_backend_hydro_tabular4
    arch_cuda_backend_diffusion
    arch_cuda_backend_exchange
    arch_cuda_backend_amr_flux)

# Runtime/store control and immutable EOS uploads contain no kernels.
# Keep each immutable owner in the translation unit matching its resource
# lifetime; this avoids one table-heavy umbrella unit while retaining a
# single public loader API.
# Give each functionally split Host unit its own object target.  The named
# compile pool above provides the hard Ninja scheduling bound, while the
# explicit target order documents the intended generator-neutral lane.
function(arch_configure_cuda_host_object target source debug_level)
    add_library(${target} OBJECT ${source})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${ARCH_CUSTOM_REGISTRY_DIR}")
    target_link_libraries(${target} PRIVATE
        arch_build_contract CUDA::cudart)
    target_compile_options(${target} PRIVATE
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:${debug_level}>)
    set_target_properties(${target} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION OFF)
    if(ARCH_CUDA_HEAVY_JOB_POOL)
        set_property(TARGET ${target} PROPERTY JOB_POOL_COMPILE
            ${ARCH_CUDA_HEAVY_JOB_POOL})
    endif()
endfunction()

# Immutable table owners are support code; omit their large template
# graphs from DWARF.  Operational control units retain line diagnostics.
arch_configure_cuda_host_object(arch_cuda_backend_eos_utils
    src/cuda/microphysics/device_eos_owner_utils.cpp -g0)
arch_configure_cuda_host_object(arch_cuda_backend_eos_species
    src/cuda/microphysics/device_species_owner.cpp -g0)
arch_configure_cuda_host_object(arch_cuda_backend_eos_helm
    src/cuda/microphysics/helm_eos_device_owner.cpp -g0)
arch_configure_cuda_host_object(arch_cuda_backend_eos_tabular3
    src/cuda/microphysics/tabular3_eos_device_owner.cpp -g0)
arch_configure_cuda_host_object(arch_cuda_backend_eos_tabular4
    src/cuda/microphysics/tabular4_eos_device_owner.cpp -g0)
arch_configure_cuda_host_object(arch_cuda_backend_resources
    src/cuda/runtime/control/CudaBackendResources.cpp -g1)
arch_configure_cuda_host_object(arch_cuda_backend_core
    src/cuda/runtime/control/CudaBackendCore.cpp -g1)
arch_configure_cuda_host_object(arch_cuda_backend_factory
    src/cuda/runtime/CudaBackendFactory.cpp -g1)
arch_configure_cuda_host_object(arch_cuda_backend_hydro_control
    src/cuda/runtime/hydro/CudaBackendHydroControl.cpp -g1)
arch_configure_cuda_host_object(arch_cuda_backend_microphysics_control
    src/cuda/runtime/control/CudaBackendMicrophysicsControl.cpp -g1)
arch_configure_cuda_host_object(arch_cuda_backend_store
    src/cuda/runtime/control/CudaBackendStore.cpp -g1)
arch_configure_cuda_host_object(arch_cuda_backend_indicators
    src/cuda/runtime/amr/CudaBackendIndicators.cpp -g1)
arch_configure_cuda_host_object(arch_cuda_backend_migration
    src/cuda/runtime/amr/CudaBackendMigration.cpp -g1)

# Keep the host-owner phase ordered without repeating each adjacent edge.
arch_order_cuda_targets(
    arch_cuda_backend_eos_utils
    arch_cuda_backend_eos_species
    arch_cuda_backend_eos_helm
    arch_cuda_backend_eos_tabular3
    arch_cuda_backend_eos_tabular4
    arch_cuda_backend_resources
    arch_cuda_backend_core
    arch_cuda_backend_factory
    arch_cuda_backend_hydro_control
    arch_cuda_backend_microphysics_control
    arch_cuda_backend_store)

# Finish host control before the bounded device-template pool. Broad outer
# parallelism can otherwise overlap several large host/device frontends.
# The phase barrier is not a memory guarantee; measure the selected heavy
# pool limit and retain system headroom under the build memory guard.
add_dependencies(arch_cuda_backend_burn_ideal
    arch_cuda_backend_store)

add_library(arch_cuda_backend STATIC
    $<TARGET_OBJECTS:arch_cuda_backend_grid_metrics>
    $<TARGET_OBJECTS:arch_cuda_backend_migration>
    $<TARGET_OBJECTS:arch_cuda_backend_amr_migration>
    $<TARGET_OBJECTS:arch_cuda_backend_amr_indicators>
    $<TARGET_OBJECTS:arch_cuda_backend_indicators>
    $<TARGET_OBJECTS:arch_cuda_backend_eos_utils>
    $<TARGET_OBJECTS:arch_cuda_backend_eos_species>
    $<TARGET_OBJECTS:arch_cuda_backend_eos_helm>
    $<TARGET_OBJECTS:arch_cuda_backend_eos_tabular3>
    $<TARGET_OBJECTS:arch_cuda_backend_eos_tabular4>
    $<TARGET_OBJECTS:arch_cuda_backend_resources>
    $<TARGET_OBJECTS:arch_cuda_backend_core>
    $<TARGET_OBJECTS:arch_cuda_backend_factory>
    $<TARGET_OBJECTS:arch_cuda_backend_hydro_control>
    $<TARGET_OBJECTS:arch_cuda_backend_microphysics_control>
    $<TARGET_OBJECTS:arch_cuda_backend_store>
    $<TARGET_OBJECTS:arch_cuda_backend_burn_ideal>
    $<TARGET_OBJECTS:arch_cuda_backend_burn_helm>
    $<TARGET_OBJECTS:arch_cuda_backend_burn_tabular3d>
    $<TARGET_OBJECTS:arch_cuda_backend_burn_tabular3d_aprox13>
    $<TARGET_OBJECTS:arch_cuda_backend_burn_tabular3d_aprox19>
    $<TARGET_OBJECTS:arch_cuda_backend_burn_tabular3d_aprox21>
    $<TARGET_OBJECTS:arch_cuda_backend_burn_tabular3d_iso7>
    $<TARGET_OBJECTS:arch_cuda_backend_burn_tabular4d>
    $<TARGET_OBJECTS:arch_cuda_backend_burn_tabular4d_aprox13>
    $<TARGET_OBJECTS:arch_cuda_backend_burn_tabular4d_aprox19>
    $<TARGET_OBJECTS:arch_cuda_backend_burn_tabular4d_aprox21>
    $<TARGET_OBJECTS:arch_cuda_backend_burn_tabular4d_iso7>
    $<TARGET_OBJECTS:arch_cuda_backend_hydro_ideal>
    $<TARGET_OBJECTS:arch_cuda_backend_hydro_helm>
    $<TARGET_OBJECTS:arch_cuda_backend_hydro_tabular3>
    $<TARGET_OBJECTS:arch_cuda_backend_hydro_tabular4>
    $<TARGET_OBJECTS:arch_cuda_backend_diffusion>
    $<TARGET_OBJECTS:arch_cuda_backend_exchange>
    $<TARGET_OBJECTS:arch_cuda_backend_amr_flux>)
target_compile_features(arch_cuda_backend PRIVATE cxx_std_20)
target_include_directories(arch_cuda_backend PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${ARCH_CUSTOM_REGISTRY_DIR}")
target_link_libraries(arch_cuda_backend
    PUBLIC arch_build_contract
    PRIVATE arch_diffusion_math CUDA::cudart)
target_compile_options(arch_cuda_backend PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;-Xcompiler=-march=native>)
set_target_properties(arch_cuda_backend PROPERTIES
    CUDA_STANDARD 20
    CUDA_STANDARD_REQUIRED ON
    INTERPROCEDURAL_OPTIMIZATION OFF)
target_link_libraries(ARCH PRIVATE arch_cuda_backend)
foreach(custom_id IN LISTS ARCH_CUSTOM_CUDA_IDS)
    set(custom_type "${ARCH_CUSTOM_TYPE_${custom_id}}")
    set(custom_header "${ARCH_CUSTOM_HEADER_${custom_id}}")
    foreach(custom_eos_tag tabular3d tabular4d)
        if(custom_eos_tag STREQUAL "tabular3d")
            set(custom_eos Tabular3DEOSView)
            set(custom_eos_header "physics/eos/Tabular3DEOS.h")
        else()
            set(custom_eos Tabular4DEOSView)
            set(custom_eos_header "physics/eos/Tabular4DEOS.h")
        endif()
        set(custom_route "${ARCH_CUSTOM_REGISTRY_DIR}/burn_${custom_eos_tag}_${custom_id}.cu")
        configure_file(cmake/CudaCustomDenseRoute.cu.in "${custom_route}" @ONLY)
        set(custom_target "arch_cuda_burn_${custom_eos_tag}_${custom_id}")
        arch_configure_cuda_backend_object(${custom_target} "${custom_route}")
        target_sources(arch_cuda_backend PRIVATE $<TARGET_OBJECTS:${custom_target}>)
    endforeach()
endforeach()
include(cmake/CudaBurnDenseRoutes.cmake)
arch_register_cuda_dense_burn_routes()
include(cmake/CudaBurnSparseRoutes.cmake)
arch_register_cuda_sparse_burn_routes()
# A full CUDA build has three explicit phases: backend archive, dispatch
# archive, and then the application.  The file-backed barriers are stronger
# than target-level object ordering under Ninja and prevent unplanned
# overlap between these phases. They do not replace memory measurements.
arch_add_completion_barrier(
    arch_cuda_backend_complete arch_cuda_backend)
add_dependencies(arch_solver_dispatch arch_cuda_backend_complete)
if(ARCH_CUDA_HEAVY_JOB_POOL)
    set_property(TARGET arch_solver_dispatch PROPERTY JOB_POOL_COMPILE
        ${ARCH_CUDA_HEAVY_JOB_POOL})
    set_property(TARGET arch_solver_dispatch PROPERTY JOB_POOL_LINK
        ${ARCH_CUDA_HEAVY_JOB_POOL})
endif()
arch_add_completion_barrier(
    arch_solver_dispatch_complete arch_solver_dispatch)
add_dependencies(ARCH arch_solver_dispatch_complete)
if(BUILD_TESTING)
    set_property(TARGET arch_runtime_probe_capabilities APPEND PROPERTY
        BUILD_RPATH "${CMAKE_CUDA_IMPLICIT_LINK_DIRECTORIES}")
endif()
