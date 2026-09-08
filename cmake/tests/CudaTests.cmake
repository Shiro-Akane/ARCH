# Focused CUDA tests. Included only after the production CUDA targets exist.
# Target-specific language, numerical options and ownership are intentional.

# These helpers change compile properties only: sources, owners, libraries and
# test registration remain visible at their individual declarations.
function(arch_configure_cuda_math_test target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    set(cuda_options --expt-relaxed-constexpr)
    list(APPEND cuda_options ${ARGN})
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:CUDA>:${cuda_options}>")
    set_target_properties(${target} PROPERTIES
        CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON)
endfunction()

# Leaf witnesses inherit host features from their original dependencies.
# Keep their explicit IPO-off contract distinct from the broader math tests.
function(arch_configure_cuda_leaf_test target)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
    set_target_properties(${target} PROPERTIES
        CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON INTERPROCEDURAL_OPTIMIZATION OFF)
endfunction()

function(arch_configure_generated_cuda_test target custom_id)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>
        "-include" "${ARCH_CUSTOM_HEADER_${custom_id}}")
    target_compile_definitions(${target} PRIVATE
        ARCH_TEST_NETWORK_TYPE=${ARCH_CUSTOM_TYPE_${custom_id}})
    set_target_properties(${target} PROPERTIES
        CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON INTERPROCEDURAL_OPTIMIZATION OFF)
    if(ARCH_CUDA_HEAVY_JOB_POOL)
        set_property(TARGET ${target} PROPERTY JOB_POOL_COMPILE ${ARCH_CUDA_HEAVY_JOB_POOL})
    endif()
endfunction()

# Generated-network mathematical and trajectory witnesses.
foreach(custom_id IN LISTS ARCH_CUSTOM_CUDA_IDS)
    set(custom_test "arch_cuda_generated_math_${custom_id}")
    add_executable(${custom_test} tests/cuda/test_generated_network_math.cu)
    arch_configure_generated_cuda_test(${custom_test} "${custom_id}")
    target_link_libraries(${custom_test} PRIVATE CUDA::cudart)
    add_test(NAME cuda_generated_math_${custom_id} COMMAND ${custom_test})
    set_tests_properties(cuda_generated_math_${custom_id} PROPERTIES SKIP_RETURN_CODE 77)
    if(ARCH_CUSTOM_EQUATIONS_${custom_id} GREATER 31
       AND ARCH_ENABLE_KLU AND TARGET arch_cuda_sparse_provider)
        # Real generated trajectories through the production sparse owner.
        # State/duration/composition are validation inputs, never network-ID
        # dependent defaults or a substitute manufactured matrix.
        set(sparse_test "arch_cuda_generated_sparse_burn_${custom_id}")
        add_executable(${sparse_test}
            tests/cuda/test_generated_sparse_burn.cpp
            tests/cuda/test_generated_sparse_burn_factory.cu)
        arch_configure_generated_cuda_test(${sparse_test} "${custom_id}")
        target_link_libraries(${sparse_test} PRIVATE arch_build_contract arch_cuda_sparse_provider)
    endif()
    if(ARCH_CUSTOM_AUXILIARY_${custom_id} EQUAL 1
       AND ARCH_CUSTOM_EQUATIONS_${custom_id} LESS_EQUAL 31)
        # These controls require explicit physical state arguments supplied
        # by the module's validation recipe, not arbitrary universal CTest
        # defaults for every possible user-generated reaction network.
        foreach(weak_control trajectory factory)
            set(weak_test "arch_cuda_generated_weak_${weak_control}_${custom_id}")
            add_executable(${weak_test}
                "tests/cuda/test_generated_weak_${weak_control}.cu")
            arch_configure_generated_cuda_test(${weak_test} "${custom_id}")
            target_link_libraries(${weak_test} PRIVATE arch_build_contract CUDA::cudart)
            if(weak_control STREQUAL "trajectory")
                # Consume table owners through their canonical archive;
                # do not publish the same OBJECT ownership in a second target.
                target_link_libraries(${weak_test} PRIVATE arch_cuda_backend)
                target_compile_definitions(${weak_test} PRIVATE ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
            endif()
            if(weak_control STREQUAL "factory" AND TARGET arch_cuda_sparse_provider)
                target_link_libraries(${weak_test} PRIVATE arch_cuda_sparse_provider)
            endif()
        endforeach()
    endif()
endforeach()

# Backend resource ownership, AMR transactions and geometric leaves.
add_executable(arch_cuda_regrid_transaction tests/cuda/test_cuda_regrid_transaction.cpp)
target_link_libraries(arch_cuda_regrid_transaction PRIVATE
    arch_cuda_backend CUDA::cudart)
add_test(NAME cuda_regrid_transaction COMMAND arch_cuda_regrid_transaction)
set_tests_properties(cuda_regrid_transaction PROPERTIES SKIP_RETURN_CODE 77)
# Exercise the same setup kernel owned by the production backend without
# compiling its unrelated hydro/burn instantiation matrix.
add_executable(arch_cuda_grid_metrics_cache
    tests/cuda/test_grid_metrics_cache.cu
    $<TARGET_OBJECTS:arch_cuda_backend_grid_metrics>)
target_link_libraries(arch_cuda_grid_metrics_cache PRIVATE
    arch_build_contract CUDA::cudart)
arch_configure_cuda_leaf_test(arch_cuda_grid_metrics_cache)
if(ARCH_CUDA_HEAVY_JOB_POOL)
    set_property(TARGET arch_cuda_grid_metrics_cache PROPERTY
        JOB_POOL_COMPILE ${ARCH_CUDA_HEAVY_JOB_POOL})
    set_property(TARGET arch_cuda_grid_metrics_cache PROPERTY
        JOB_POOL_LINK ${ARCH_CUDA_HEAVY_JOB_POOL})
endif()
add_test(NAME cuda_grid_metrics_cache COMMAND arch_cuda_grid_metrics_cache)
set_tests_properties(cuda_grid_metrics_cache PROPERTIES SKIP_RETURN_CODE 77)
if(TARGET arch_cuda_sparse_provider)
    add_executable(arch_cuda_sparse_burn_factory tests/cuda/test_cuda_sparse_burn_factory.cpp)
    target_link_libraries(arch_cuda_sparse_burn_factory PRIVATE arch_cuda_backend CUDA::cudart)
    add_test(NAME cuda_sparse_burn_factory COMMAND arch_cuda_sparse_burn_factory)
    set_tests_properties(cuda_sparse_burn_factory PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(arch_cuda_sparse_be_nr_batch tests/cuda/test_sparse_be_nr_batch.cu)
    target_link_libraries(arch_cuda_sparse_be_nr_batch PRIVATE arch_cuda_sparse_provider)
    arch_configure_cuda_leaf_test(arch_cuda_sparse_be_nr_batch)
    add_test(NAME cuda_sparse_be_nr_batch COMMAND arch_cuda_sparse_be_nr_batch)
    set_tests_properties(cuda_sparse_be_nr_batch PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(arch_cuda_cudss_sparse_solver tests/cuda/test_cudss_sparse_solver.cpp)
    target_link_libraries(arch_cuda_cudss_sparse_solver PRIVATE arch_cuda_sparse_provider)
    add_test(NAME cuda_cudss_sparse_solver COMMAND arch_cuda_cudss_sparse_solver)
    set_tests_properties(cuda_cudss_sparse_solver PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(arch_cuda_burn_eos_failure tests/cuda/test_burn_eos_failure.cu)
    target_link_libraries(arch_cuda_burn_eos_failure PRIVATE arch_cuda_sparse_provider)
    arch_configure_cuda_leaf_test(arch_cuda_burn_eos_failure)
    if(ARCH_CUDA_HEAVY_JOB_POOL)
        set_property(TARGET arch_cuda_burn_eos_failure PROPERTY JOB_POOL_COMPILE ${ARCH_CUDA_HEAVY_JOB_POOL})
    endif()
    add_test(NAME cuda_burn_eos_failure COMMAND arch_cuda_burn_eos_failure)
    set_tests_properties(cuda_burn_eos_failure PROPERTIES SKIP_RETURN_CODE 77)
endif()
add_executable(arch_cuda_regrid_migration
    tests/cuda/test_cuda_regrid_migration.cu
    $<TARGET_OBJECTS:arch_cuda_backend_amr_migration>)
target_link_libraries(arch_cuda_regrid_migration PRIVATE arch_build_contract CUDA::cudart)
arch_configure_cuda_leaf_test(arch_cuda_regrid_migration)
add_test(NAME cuda_regrid_migration COMMAND arch_cuda_regrid_migration)
set_tests_properties(cuda_regrid_migration PROPERTIES SKIP_RETURN_CODE 77)
add_executable(arch_cuda_curvilinear_geometry_smoke
    tests/cuda/test_curvilinear_geometry_smoke.cu)
target_link_libraries(arch_cuda_curvilinear_geometry_smoke PRIVATE
    arch_build_contract CUDA::cudart arch_diffusion_math)
arch_configure_cuda_leaf_test(arch_cuda_curvilinear_geometry_smoke)
add_test(NAME cuda_curvilinear_geometry_smoke COMMAND arch_cuda_curvilinear_geometry_smoke)
set_tests_properties(cuda_curvilinear_geometry_smoke PROPERTIES SKIP_RETURN_CODE 77)
add_executable(arch_cuda_hydro_eos_failure tests/cuda/test_hydro_eos_failure.cu)
target_link_libraries(arch_cuda_hydro_eos_failure PRIVATE arch_build_contract CUDA::cudart)
arch_configure_cuda_leaf_test(arch_cuda_hydro_eos_failure)
if(ARCH_CUDA_HEAVY_JOB_POOL)
    set_property(TARGET arch_cuda_hydro_eos_failure PROPERTY JOB_POOL_COMPILE ${ARCH_CUDA_HEAVY_JOB_POOL})
endif()
add_test(NAME cuda_hydro_eos_failure COMMAND arch_cuda_hydro_eos_failure)
set_tests_properties(cuda_hydro_eos_failure PROPERTIES SKIP_RETURN_CODE 77)
# Host-only registry execution compiled by NVCC: catches closure-lowering
# regressions without instantiating the full Hydro numerical route matrix.
add_executable(arch_cuda_hydro_dispatch tests/cuda/test_hydro_dispatch.cu)
target_link_libraries(arch_cuda_hydro_dispatch PRIVATE arch_build_contract CUDA::cudart)
arch_configure_cuda_leaf_test(arch_cuda_hydro_dispatch)
if(ARCH_CUDA_HEAVY_JOB_POOL)
    set_property(TARGET arch_cuda_hydro_dispatch PROPERTY JOB_POOL_COMPILE ${ARCH_CUDA_HEAVY_JOB_POOL})
    set_property(TARGET arch_cuda_hydro_dispatch PROPERTY JOB_POOL_LINK ${ARCH_CUDA_HEAVY_JOB_POOL})
endif()
add_test(NAME cuda_hydro_dispatch COMMAND arch_cuda_hydro_dispatch)
add_executable(arch_cuda_refinement_indicators
    tests/cuda/test_refinement_indicators.cpp
    $<TARGET_OBJECTS:arch_cuda_backend_amr_indicators>)
target_link_libraries(arch_cuda_refinement_indicators PRIVATE
    arch_build_contract CUDA::cudart)
add_test(NAME cuda_refinement_indicators COMMAND arch_cuda_refinement_indicators)
set_tests_properties(cuda_refinement_indicators PROPERTIES SKIP_RETURN_CODE 77)

# These mathematical regressions use the same build contract as production
# without requiring the complete template-heavy backend archive.
add_executable(arch_cuda_compensated_sum tests/cuda/test_compensated_sum.cu)
target_compile_features(arch_cuda_compensated_sum PRIVATE cxx_std_20)
arch_configure_cuda_leaf_test(arch_cuda_compensated_sum)
add_test(NAME cuda_compensated_sum COMMAND arch_cuda_compensated_sum)
set_tests_properties(cuda_compensated_sum PROPERTIES SKIP_RETURN_CODE 77)
add_executable(arch_cuda_amr_composition
    tests/cuda/test_cuda_amr_composition.cu)
target_compile_features(arch_cuda_amr_composition PRIVATE cxx_std_20)
arch_configure_cuda_leaf_test(arch_cuda_amr_composition)
target_link_libraries(arch_cuda_amr_composition PRIVATE CUDA::cudart)
add_test(NAME cuda_amr_composition COMMAND arch_cuda_amr_composition)
set_tests_properties(cuda_amr_composition PROPERTIES SKIP_RETURN_CODE 77)

# Mathematical policy witnesses and immutable EOS resource owners.
add_executable(arch_cuda_compile_probe tests/cuda/test_cuda_compile_probe.cu)
arch_configure_cuda_math_test(arch_cuda_compile_probe)
add_executable(arch_cuda_eos_host_device_parity
    tests/cuda/test_eos_host_device_parity.cu)
arch_configure_cuda_math_test(arch_cuda_eos_host_device_parity)
target_compile_definitions(arch_cuda_eos_host_device_parity PRIVATE
    ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(arch_cuda_eos_host_device_parity PRIVATE
    arch_cuda_backend)
add_executable(arch_cuda_tabular_free_energy_owner
    tests/cuda/test_tabular_free_energy_owner.cu)
arch_configure_cuda_math_test(arch_cuda_tabular_free_energy_owner)
target_link_libraries(arch_cuda_tabular_free_energy_owner PRIVATE
    arch_cuda_backend)
add_executable(arch_cuda_network_nse_device
    tests/cuda/test_network_nse_device.cu)
arch_configure_cuda_math_test(arch_cuda_network_nse_device)
add_executable(arch_cuda_hydro_leaf_parity
    tests/cuda/test_hydro_leaf_parity.cu)
arch_configure_cuda_math_test(arch_cuda_hydro_leaf_parity)
target_link_libraries(arch_cuda_hydro_leaf_parity PRIVATE
    arch_build_contract)
add_executable(arch_cuda_reduction_contract
    tests/cuda/test_cuda_reduction_contract.cu)
arch_configure_cuda_math_test(arch_cuda_reduction_contract)
add_executable(arch_cuda_burn_policy_parity
    tests/cuda/test_burn_policy_parity.cu
    src/core/FileFingerprint.cpp)
arch_configure_cuda_math_test(arch_cuda_burn_policy_parity
    "-Xcompiler=-march=native,-fno-inline,-fno-tree-vectorize,-fno-tree-slp-vectorize")
target_compile_definitions(arch_cuda_burn_policy_parity PRIVATE
    ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
# This mathematical policy witness instantiates the shared headers and
# needs only the production immutable Helmholtz resource owners. Reuse
# their actual objects without requiring the unrelated runtime/Hydro
# matrix or compiling a second copy of any owner implementation.
target_link_libraries(arch_cuda_burn_policy_parity PRIVATE
    arch_cuda_backend_eos_helm
    arch_cuda_backend_eos_species
    arch_cuda_backend_eos_utils
    arch_build_contract CUDA::cudart)
add_executable(arch_cuda_burn_controller_parity
    tests/cuda/test_burn_controller_parity.cu)
arch_configure_cuda_math_test(arch_cuda_burn_controller_parity)
target_compile_definitions(arch_cuda_burn_controller_parity PRIVATE
    ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(arch_cuda_burn_controller_parity PRIVATE
    arch_cuda_backend_eos_helm arch_cuda_backend_eos_species
    arch_cuda_backend_eos_utils arch_build_contract CUDA::cudart)
add_test(NAME burn_controller_parity COMMAND arch_cuda_burn_controller_parity)
set_tests_properties(burn_controller_parity PROPERTIES SKIP_RETURN_CODE 77)
add_executable(arch_cuda_network_derivative tests/cuda/test_network_derivative.cu)
arch_configure_cuda_math_test(arch_cuda_network_derivative)
target_link_libraries(arch_cuda_network_derivative PRIVATE arch_build_contract CUDA::cudart)
add_test(NAME cuda_network_derivative COMMAND arch_cuda_network_derivative)
set_tests_properties(cuda_network_derivative PROPERTIES SKIP_RETURN_CODE 77)
add_executable(arch_cuda_burn_thermal_math tests/cuda/test_burn_thermal_math.cu)
arch_configure_cuda_math_test(arch_cuda_burn_thermal_math)
target_link_libraries(arch_cuda_burn_thermal_math PRIVATE arch_build_contract CUDA::cudart)
add_test(NAME cuda_burn_thermal_math COMMAND arch_cuda_burn_thermal_math)
set_tests_properties(cuda_burn_thermal_math PROPERTIES SKIP_RETURN_CODE 77)

# Host scheduler witnesses reuse the production backend; these .cu files stay C++.
add_executable(arch_cuda_hydro_block
    tests/cuda/test_cuda_hydro_block.cu)
# This is a Host scheduler/controller witness linked to the one
# production CUDA backend.  It intentionally owns no device code, so
# compile the .cu-named focused fixture as ordinary C++ and keep all
# kernels in arch_cuda_backend.
set_source_files_properties(tests/cuda/test_cuda_hydro_block.cu
    PROPERTIES
        LANGUAGE CXX
        COMPILE_FLAGS "-x c++")
arch_configure_cuda_math_test(arch_cuda_hydro_block "-Xcompiler=-march=native")
target_compile_definitions(arch_cuda_hydro_block PRIVATE
    ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(arch_cuda_hydro_block PRIVATE
    arch_cuda_backend arch_solver_dispatch CUDA::cudart)
add_executable(arch_cuda_multiblock_hydro
    tests/cuda/test_cuda_multiblock_hydro.cu)
set_source_files_properties(tests/cuda/test_cuda_multiblock_hydro.cu
    PROPERTIES LANGUAGE CXX COMPILE_FLAGS "-x c++")
target_compile_features(arch_cuda_multiblock_hydro PRIVATE cxx_std_20)
target_include_directories(arch_cuda_multiblock_hydro PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(arch_cuda_multiblock_hydro PROPERTIES
    CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON)
target_link_libraries(arch_cuda_multiblock_hydro PRIVATE
    arch_cuda_backend arch_solver_dispatch CUDA::cudart)
add_executable(arch_cuda_multiblock_diffusion
    tests/cuda/test_cuda_multiblock_diffusion.cu)
set_source_files_properties(tests/cuda/test_cuda_multiblock_diffusion.cu
    PROPERTIES LANGUAGE CXX COMPILE_FLAGS "-x c++")
target_compile_features(arch_cuda_multiblock_diffusion PRIVATE cxx_std_20)
target_include_directories(arch_cuda_multiblock_diffusion PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(arch_cuda_multiblock_diffusion PRIVATE
    arch_cuda_backend arch_solver_dispatch)
add_executable(arch_cuda_multiblock_burn
    tests/cuda/test_cuda_multiblock_burn.cu)
set_source_files_properties(tests/cuda/test_cuda_multiblock_burn.cu
    PROPERTIES LANGUAGE CXX COMPILE_FLAGS "-x c++")
target_compile_features(arch_cuda_multiblock_burn PRIVATE cxx_std_20)
target_include_directories(arch_cuda_multiblock_burn PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(arch_cuda_multiblock_burn PRIVATE
    ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(arch_cuda_multiblock_burn PRIVATE
    arch_cuda_backend arch_solver_dispatch)
add_executable(arch_cuda_store_lifecycle
    tests/cuda/test_cuda_store_lifecycle.cpp)
target_compile_features(arch_cuda_store_lifecycle PRIVATE cxx_std_20)
target_include_directories(arch_cuda_store_lifecycle PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(arch_cuda_store_lifecycle PRIVATE
    arch_cuda_backend arch_solver_dispatch CUDA::cudart)
add_executable(arch_cuda_amr_exchange
    tests/cuda/test_cuda_amr_exchange.cpp)
target_compile_features(arch_cuda_amr_exchange PRIVATE cxx_std_20)
target_include_directories(arch_cuda_amr_exchange PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(arch_cuda_amr_exchange PRIVATE
    arch_cuda_backend arch_solver_dispatch CUDA::cudart)
add_executable(arch_cuda_single_level_validation
    tests/cuda/test_cuda_single_level_validation.cpp)
target_compile_features(arch_cuda_single_level_validation PRIVATE
    cxx_std_20)
target_include_directories(arch_cuda_single_level_validation PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(arch_cuda_single_level_validation PRIVATE
    arch_cuda_backend)
add_test(NAME checkpoint_temporal_comparison
    COMMAND arch_cuda_single_level_validation --test-time-comparison)
add_executable(arch_cuda_diffusion_rkl_parity
    tests/cuda/test_diffusion_rkl_parity.cu)
arch_configure_cuda_math_test(arch_cuda_diffusion_rkl_parity "-Xcompiler=-march=native")
target_link_libraries(arch_cuda_diffusion_rkl_parity PRIVATE
    arch_cuda_backend)
add_executable(arch_cuda_boundary_plan_parity
    tests/cuda/test_boundary_plan_parity.cu)
arch_configure_cuda_math_test(arch_cuda_boundary_plan_parity)
add_executable(arch_cuda_policy_resolution
    tests/cuda/test_cuda_policy_resolution.cu)
arch_configure_cuda_math_test(arch_cuda_policy_resolution)
target_link_libraries(arch_cuda_policy_resolution PRIVATE
    arch_build_contract)
# Bound test compiler/linker concurrency independently of registration below.
if(ARCH_CUDA_HEAVY_JOB_POOL)
    # Focused CUDA validation is outside the production ARCH target,
    # but a default test build may schedule it at the same time.  Use
    # the same bounded lane for every CUDA-facing test compiler and
    # linker.  In testing builds ARCH joins that lane as well, avoiding
    # an ordinary application compile alongside a multi-GiB NVCC edge.
    foreach(target IN ITEMS
            arch_burn_mainline_reference
            arch_cuda_compensated_sum
            arch_cuda_amr_composition
            arch_cuda_compile_probe
            arch_cuda_eos_host_device_parity
            arch_cuda_tabular_free_energy_owner
            arch_cuda_network_nse_device
            arch_cuda_hydro_leaf_parity
            arch_cuda_reduction_contract
            arch_cuda_burn_policy_parity
            arch_cuda_burn_controller_parity
            arch_cuda_network_derivative
            arch_cuda_burn_thermal_math
            arch_cuda_hydro_block
            arch_cuda_multiblock_hydro
            arch_cuda_multiblock_diffusion
            arch_cuda_multiblock_burn
            arch_cuda_store_lifecycle
            arch_cuda_amr_exchange
            arch_cuda_single_level_validation
            arch_cuda_diffusion_rkl_parity
            arch_cuda_boundary_plan_parity
            arch_cuda_policy_resolution)
        set_property(TARGET ${target} PROPERTY JOB_POOL_COMPILE
            ${ARCH_CUDA_HEAVY_JOB_POOL})
        set_property(TARGET ${target} PROPERTY JOB_POOL_LINK
            ${ARCH_CUDA_HEAVY_JOB_POOL})
    endforeach()
    set_property(TARGET ARCH PROPERTY JOB_POOL_COMPILE
        ${ARCH_CUDA_HEAVY_JOB_POOL})
    set_property(TARGET ARCH PROPERTY JOB_POOL_LINK
        ${ARCH_CUDA_HEAVY_JOB_POOL})
endif()
# Runtime gates preserve their registration order and explicit prerequisites.
add_test(NAME cuda_compile_probe COMMAND arch_cuda_compile_probe)
add_test(NAME hydro_leaf_parity COMMAND arch_cuda_hydro_leaf_parity)
add_test(NAME eos_host_device_parity COMMAND arch_cuda_eos_host_device_parity)
add_test(NAME tabular_free_energy_owner
    COMMAND arch_cuda_tabular_free_energy_owner)
set_tests_properties(tabular_free_energy_owner PROPERTIES
    SKIP_RETURN_CODE 77)
add_test(NAME network_nse_device COMMAND arch_cuda_network_nse_device)
add_test(NAME burn_policy_parity_helpers
    COMMAND arch_cuda_burn_policy_parity helpers)
add_test(NAME diffusion_rkl_parity
    COMMAND arch_cuda_diffusion_rkl_parity)
add_test(NAME boundary_plan_parity
    COMMAND arch_cuda_boundary_plan_parity)
add_test(NAME cuda_policy_resolution
    COMMAND arch_cuda_policy_resolution)
add_test(NAME cuda_reduction_contract
    COMMAND arch_cuda_reduction_contract)
# This is the first real-device gate: the default witness allocates a
# CUDA backend, transfers state, launches Hydro and RKL kernels, and
# materializes the result.  Keep the result validator below out of
# CTest because it requires explicit trace/checkpoint inputs.
add_test(NAME cuda_single_level_smoke COMMAND arch_cuda_hydro_block)
add_test(NAME cuda_multiblock_hydro
    COMMAND arch_cuda_multiblock_hydro)
add_test(NAME cuda_multiblock_diffusion
    COMMAND arch_cuda_multiblock_diffusion)
add_test(NAME cuda_multiblock_burn
    COMMAND arch_cuda_multiblock_burn)
add_test(NAME cuda_store_lifecycle
    COMMAND arch_cuda_store_lifecycle)
set_tests_properties(cuda_store_lifecycle PROPERTIES
    SKIP_RETURN_CODE 77)
add_test(NAME cuda_amr_exchange COMMAND arch_cuda_amr_exchange)
set_tests_properties(cuda_amr_exchange PROPERTIES
    SKIP_RETURN_CODE 77)
add_test(NAME cuda_hydro_route_matrix
    COMMAND arch_cuda_hydro_block hydro-matrix)
add_test(NAME cuda_hydro_integrator_matrix
    COMMAND arch_cuda_hydro_block hydro-integrators)
add_test(NAME cuda_backend_eos_owner_matrix
    COMMAND arch_cuda_hydro_block eos-matrix)
add_test(NAME cuda_backend_diffusion_rkl1
    COMMAND arch_cuda_hydro_block rkl1)
add_test(NAME cuda_backend_diffusion_rkl2
    COMMAND arch_cuda_hydro_block rkl2)
foreach(route IN ITEMS
        aprox13.be_nr aprox13.bd aprox13.ros4
        aprox19.be_nr aprox19.bd aprox19.ros4
        aprox21.be_nr aprox21.bd aprox21.ros4
        iso7.be_nr iso7.bd iso7.ros4)
    string(REPLACE "." "_" backend_route_test "${route}")
    add_test(NAME "cuda_backend_burn_${backend_route_test}"
        COMMAND arch_cuda_hydro_block "${route}")
endforeach()
foreach(solver IN ITEMS be_nr bd ros4)
    add_test(NAME "burn_policy_parity_status_${solver}"
        COMMAND arch_cuda_burn_policy_parity "status.${solver}")
endforeach()
foreach(route IN ITEMS
        aprox13.be_nr aprox13.bd aprox13.ros4
        aprox19.be_nr aprox19.bd aprox19.ros4
        aprox21.be_nr aprox21.bd aprox21.ros4
        iso7.be_nr iso7.bd iso7.ros4)
    string(REPLACE "." "_" route_test "${route}")
    add_test(NAME "burn_policy_parity_${route_test}"
        COMMAND arch_cuda_burn_policy_parity "${route}")
    set_tests_properties("burn_policy_parity_${route_test}" PROPERTIES
        DEPENDS burn_mainline_reference)
endforeach()
