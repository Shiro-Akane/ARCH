# Host contract tests. Included only when BUILD_TESTING is enabled.
# Keep HDF5-dependent regression registration after dependency discovery.

# Shared declarations only; individual tests retain their libraries and macros.
function(arch_configure_host_test target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests" ${ARGN})
endfunction()

# Python tooling contracts.
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
# This utility reads Host HDF5 checkpoints/traces; its historical target name
# stays stable for validation runners. Its source and acceptance logic are
# unchanged, and it does not construct or link the production CUDA backend.
add_executable(arch_cuda_single_level_validation
    tests/host/io/test_cuda_single_level_validation.cpp)
arch_configure_host_test(arch_cuda_single_level_validation)
target_link_libraries(arch_cuda_single_level_validation PRIVATE arch_build_contract)
add_test(NAME checkpoint_temporal_comparison
    COMMAND arch_cuda_single_level_validation --test-time-comparison)

add_executable(arch_preview_initial_conversion tests/api/preview/test_initial_conversion.cpp)
arch_configure_host_test(arch_preview_initial_conversion)
add_test(NAME preview_initial_conversion COMMAND arch_preview_initial_conversion)
add_test(NAME preview_api_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/api/preview/test_preview.py
        $<TARGET_FILE:ARCH> ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(preview_api_contract PROPERTIES TIMEOUT 180)
add_test(NAME configuration_api_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/api/configuration/test_configuration.py
        $<TARGET_FILE:ARCH> ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(configuration_api_contract PROPERTIES TIMEOUT 180)
add_executable(arch_preview_parameter_reads tests/api/configuration/test_parameter_reads.cpp src/api/configuration/ParameterMetadata.cpp)
arch_configure_host_test(arch_preview_parameter_reads)
add_test(NAME preview_parameter_reads COMMAND arch_preview_parameter_reads)
add_test(NAME preview_parameter_metadata
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/api/configuration/test_parameter_metadata.py
        $<TARGET_FILE:ARCH> ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(preview_parameter_metadata PROPERTIES TIMEOUT 180)
add_executable(arch_preview_sampling_limits tests/api/preview/test_sampling_limits.cpp)
arch_configure_host_test(arch_preview_sampling_limits)
add_test(NAME preview_sampling_limits COMMAND arch_preview_sampling_limits)
add_test(NAME portable_network_generator
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/network/test_portable_network_generator.py)
set_tests_properties(portable_network_generator PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME cuda_amr_smoke_runner_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/validation/test_smoke_cuda_amr_runtime.py)
add_test(NAME curvilinear_manifest_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/validation/test_curvilinear_manifest.py)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME memory_guard_contract
        COMMAND ${Python3_EXECUTABLE} -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/resources/test_run_memory_guarded.py)
endif()
add_test(NAME backend_validation_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/validation/test_validate_backend_results.py)
set_tests_properties(backend_validation_contract PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME sparse_validation_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/validation/network/test_sparse_validation.py)
add_test(NAME validation_provenance_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/validation/test_validation_provenance.py)
set_tests_properties(validation_provenance_contract PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME runtime_validation_inputs_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/validation/test_runtime_validation_inputs.py)
set_tests_properties(runtime_validation_inputs_contract PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Numerical leaves and independent reference authorities.
foreach(contract IN ITEMS core/physical_constants amr/refinement_indicator_math grid/curvilinear_metrics)
    get_filename_component(contract_directory "${contract}" DIRECTORY)
    get_filename_component(contract "${contract}" NAME)
    add_executable(arch_${contract} tests/host/${contract_directory}/test_${contract}.cpp)
    arch_configure_host_test(arch_${contract})
    target_link_libraries(arch_${contract} PRIVATE arch_build_contract)
    add_test(NAME ${contract} COMMAND arch_${contract})
endforeach()
add_executable(arch_compensated_sum tests/host/numerics/test_compensated_sum.cpp)
target_include_directories(arch_compensated_sum PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_compile_features(arch_compensated_sum PRIVATE cxx_std_20)
add_test(NAME compensated_sum COMMAND arch_compensated_sum)
add_executable(arch_sparse_ode_continuation tests/host/burn/test_sparse_ode_continuation.cpp)
target_include_directories(arch_sparse_ode_continuation PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(arch_sparse_ode_continuation PRIVATE arch_build_contract)
add_test(NAME sparse_ode_continuation COMMAND arch_sparse_ode_continuation)
add_executable(arch_sparse_residual tests/host/numerics/test_sparse_residual.cpp)
target_link_libraries(arch_sparse_residual PRIVATE arch_build_contract)
add_test(NAME sparse_residual COMMAND arch_sparse_residual)
add_executable(arch_checkpoint_conservation_metrics tests/host/io/test_checkpoint_conservation_metrics.cpp)
target_include_directories(arch_checkpoint_conservation_metrics PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(arch_checkpoint_conservation_metrics PRIVATE arch_build_contract)
add_test(NAME checkpoint_conservation_metrics COMMAND arch_checkpoint_conservation_metrics)
add_executable(arch_burn_mainline_reference
    tests/host/burn/test_burn_mainline_reference.cpp src/core/files/FileFingerprint.cpp)
target_include_directories(arch_burn_mainline_reference PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_compile_features(arch_burn_mainline_reference PRIVATE cxx_std_20)
target_compile_definitions(arch_burn_mainline_reference PRIVATE
    ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
# This independent stiff reference evaluates full networks many times. Keep
# its numerical budgets and strict FP policy; optimize the test executor even
# in Debug so debug builds do not spend minutes interpreting small RHS loops.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(arch_burn_mainline_reference PRIVATE -O2)
endif()
add_test(NAME burn_mainline_reference COMMAND arch_burn_mainline_reference)
add_executable(arch_generated_nse tests/host/network/test_generated_nse.cpp)
arch_configure_host_test(arch_generated_nse)
target_link_libraries(arch_generated_nse PRIVATE arch_build_contract)
add_test(NAME generated_nse COMMAND arch_generated_nse)
add_executable(arch_tabular_strict tests/math/microphysics/test_tabular_strict.cpp)
arch_configure_host_test(arch_tabular_strict)
target_link_libraries(arch_tabular_strict PRIVATE arch_build_contract)
add_test(NAME tabular_strict_math COMMAND arch_tabular_strict)
add_executable(arch_baryon_source tests/host/eos/BaryonSourceRegression.cpp
    src/physics/eos/sources/TabularBaryonSource.cpp)
arch_configure_host_test(arch_baryon_source)
target_link_libraries(arch_baryon_source PRIVATE arch_build_contract)
add_test(NAME baryon_source_format COMMAND arch_baryon_source
    "${CMAKE_CURRENT_BINARY_DIR}/baryon-source-test-data")
add_executable(arch_helm_components tests/host/eos/HelmComponentsRegression.cpp)
arch_configure_host_test(arch_helm_components "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(arch_helm_components PRIVATE arch_build_contract)
add_test(NAME helm_components COMMAND arch_helm_components
    "${CMAKE_CURRENT_SOURCE_DIR}/EOS_toolkit/tables/helmholtz/helm_table.dat")
# These maintained recipes define the physical inputs of the detailed-balance
# and cooling-handoff witnesses. Do not impose their trajectories on arbitrary
# user networks or require pynucastro during an ordinary CPU CI run.
foreach(reference_id IN ITEMS nse_light nse_alpha)
    if(reference_id IN_LIST ARCH_CUSTOM_CUDA_IDS)
        set(reference_target arch_generated_nse_${reference_id})
        add_executable(${reference_target} tests/host/network/test_generated_nse_network.cpp)
        arch_configure_host_test(${reference_target}
            "${ARCH_CUSTOM_NETWORK_ROOT}/${reference_id}")
        target_compile_definitions(${reference_target} PRIVATE
            ARCH_TEST_NETWORK_HEADER="${ARCH_CUSTOM_HEADER_${reference_id}}"
            ARCH_TEST_NETWORK_TYPE=${ARCH_CUSTOM_TYPE_${reference_id}}
            ARCH_TEST_GENERATED_NAMESPACE=arch_pynucastro_${reference_id})
        target_link_libraries(${reference_target} PRIVATE arch_build_contract)
        add_test(NAME generated_nse_${reference_id} COMMAND ${reference_target})
    endif()
endforeach()
add_executable(arch_mainline_authority tests/cuda/runtime/test_mainline_authority.cpp)
arch_configure_host_test(arch_mainline_authority)
target_compile_definitions(arch_mainline_authority PRIVATE
    ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(arch_mainline_authority PRIVATE Threads::Threads)
add_test(NAME mainline_authority COMMAND arch_mainline_authority)

# Handles, scheduler state and backend-neutral AMR plans.
foreach(contract IN ITEMS amr/block_handle driver/state_residency)
    get_filename_component(contract_directory "${contract}" DIRECTORY)
    get_filename_component(contract "${contract}" NAME)
    add_executable(arch_${contract} tests/host/${contract_directory}/test_${contract}.cpp)
    arch_configure_host_test(arch_${contract})
    add_test(NAME ${contract} COMMAND arch_${contract})
endforeach()

add_executable(arch_shared_stage_scheduler
    tests/host/driver/test_shared_stage_scheduler.cpp)
arch_configure_host_test(arch_shared_stage_scheduler)
target_compile_definitions(arch_shared_stage_scheduler PRIVATE
    ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME shared_stage_scheduler COMMAND arch_shared_stage_scheduler)

add_executable(arch_gravity_stage_contract tests/host/gravity/test_gravity_stage_contract.cpp)
arch_configure_host_test(arch_gravity_stage_contract)
add_test(NAME gravity_stage_contract COMMAND arch_gravity_stage_contract)

foreach(contract IN ITEMS boundary_plan same_level_exchange_plan
        amr_operation_plans amr_flux_surface_plan topology_transaction)
    add_executable(arch_${contract} tests/host/amr/test_${contract}.cpp)
    arch_configure_host_test(arch_${contract})
    add_test(NAME ${contract} COMMAND arch_${contract})
endforeach()

# Runtime policy resolution and backend resource contracts.
add_executable(arch_resolved_execution_plan
    tests/host/driver/test_resolved_execution_plan.cpp)
arch_configure_host_test(arch_resolved_execution_plan "${ARCH_CUSTOM_REGISTRY_DIR}")
target_link_libraries(arch_resolved_execution_plan PRIVATE
    arch_build_contract)
add_test(NAME resolved_execution_plan COMMAND arch_resolved_execution_plan)

add_executable(arch_runtime_probe_capabilities
    tests/host/driver/test_runtime_probe_and_capabilities.cpp)
arch_configure_host_test(arch_runtime_probe_capabilities)
target_compile_definitions(arch_runtime_probe_capabilities PRIVATE
    ARCH_D1_TEST_CUDA_BUILD=$<BOOL:${ARCH_ENABLE_CUDA}>)
if(CMAKE_DL_LIBS)
    target_link_libraries(arch_runtime_probe_capabilities PRIVATE ${CMAKE_DL_LIBS})
endif()
target_link_libraries(arch_runtime_probe_capabilities PRIVATE
    arch_solver_dispatch)
add_test(NAME runtime_probe_and_capabilities
    COMMAND arch_runtime_probe_capabilities)

add_executable(arch_reduction_contract tests/host/driver/test_reduction_contract.cpp)
arch_configure_host_test(arch_reduction_contract "${ARCH_CUSTOM_REGISTRY_DIR}")
target_link_libraries(arch_reduction_contract PRIVATE
    arch_build_contract)
add_test(NAME reduction_contract COMMAND arch_reduction_contract)

add_executable(arch_compute_backend tests/host/driver/test_compute_backend.cpp)
arch_configure_host_test(arch_compute_backend)
add_test(NAME compute_backend COMMAND arch_compute_backend)

add_executable(arch_device_block_store_lifecycle
    tests/host/driver/test_device_block_store_lifecycle.cpp)
arch_configure_host_test(arch_device_block_store_lifecycle)
target_link_libraries(arch_device_block_store_lifecycle PRIVATE
    arch_build_contract)
add_test(NAME device_block_store_lifecycle
    COMMAND arch_device_block_store_lifecycle)

# These targets require HDF5/HighFive and the resolved KLU provider.
function(arch_register_io_regression_tests)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(arch_preview_mesh_checkpoint tests/api/preview/read_mesh_checkpoint.cpp)
        arch_configure_host_test(arch_preview_mesh_checkpoint
            "${highfive_SOURCE_DIR}/include" ${HDF5_INCLUDE_DIRS})
        target_link_libraries(arch_preview_mesh_checkpoint PRIVATE ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES})
        add_test(NAME ui_expansion_contract
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/api/preview/test_ui_expansion.py
                $<TARGET_FILE:ARCH> ${CMAKE_CURRENT_SOURCE_DIR} $<TARGET_FILE:arch_preview_mesh_checkpoint>)
        set_tests_properties(ui_expansion_contract PROPERTIES TIMEOUT 300)
    endif()
    add_executable(arch_preview_cellular_reference
        tests/api/preview/cellular_reference.cpp
        src/core/problem/ProblemHelper.cpp src/core/files/FileFingerprint.cpp
        src/physics/eos/eosdispatch.cpp src/physics/eos/sources/Tabular3DEOS.cpp
        src/physics/eos/sources/Tabular4DEOS.cpp src/physics/eos/sources/TabularBaryonSource.cpp
        src/physics/eos/sources/TabularCompletion.cpp)
    arch_configure_host_test(arch_preview_cellular_reference
        "${CMAKE_CURRENT_SOURCE_DIR}/simulation"
        "${highfive_SOURCE_DIR}/include" ${HDF5_INCLUDE_DIRS})
    target_link_libraries(arch_preview_cellular_reference PRIVATE
        ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
    add_test(NAME preview_cellular_2d
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/api/preview/test_cellular_preview.py
            $<TARGET_FILE:ARCH> ${CMAKE_CURRENT_SOURCE_DIR} $<TARGET_FILE:arch_preview_cellular_reference>)
    set_tests_properties(preview_cellular_2d PROPERTIES TIMEOUT 600)

    add_executable(arch_checkpoint_compatibility
        tests/host/io/test_checkpoint_compatibility.cpp
        src/core/files/FileFingerprint.cpp
        src/physics/eos/eosdispatch.cpp
        src/physics/eos/sources/TabularBaryonSource.cpp
        src/io/chk/CheckpointCompatibility.cpp
        src/io/chk/ChkIO.cpp
        src/io/hdf5/HDF5Writer.cpp)
    arch_configure_host_test(arch_checkpoint_compatibility
        "${highfive_SOURCE_DIR}/include"
        ${HDF5_INCLUDE_DIRS})
    target_link_libraries(arch_checkpoint_compatibility PRIVATE
        ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
    add_test(NAME checkpoint_compatibility
        COMMAND arch_checkpoint_compatibility
                "${CMAKE_CURRENT_BINARY_DIR}/checkpoint-compatibility-data")

    add_executable(tabular_eos_regression
        tests/host/eos/TabularEOSRegression.cpp
        src/core/files/FileFingerprint.cpp
        src/physics/eos/sources/Tabular3DEOS.cpp
        src/physics/eos/sources/Tabular4DEOS.cpp
        src/physics/eos/eosdispatch.cpp
        src/physics/eos/sources/TabularBaryonSource.cpp
        src/physics/eos/sources/TabularCompletion.cpp)
    arch_configure_host_test(tabular_eos_regression
        "${highfive_SOURCE_DIR}/include"
        ${HDF5_INCLUDE_DIRS})
    target_link_libraries(tabular_eos_regression PRIVATE
        ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
    add_test(NAME tabular_eos_ideal_gas
        COMMAND tabular_eos_regression
                "${CMAKE_CURRENT_BINARY_DIR}/tabular-eos-test-data")

    add_executable(arch_native_tabular
        tests/host/eos/NativeTabularRegression.cpp
        src/core/files/FileFingerprint.cpp
        src/physics/eos/sources/Tabular3DEOS.cpp
        src/physics/eos/eosdispatch.cpp
        src/physics/eos/sources/TabularBaryonSource.cpp
        src/physics/eos/sources/TabularCompletion.cpp)
    arch_configure_host_test(arch_native_tabular
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${highfive_SOURCE_DIR}/include" ${HDF5_INCLUDE_DIRS})
    target_link_libraries(arch_native_tabular PRIVATE
        arch_build_contract ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
    add_test(NAME native_tabular_eos COMMAND arch_native_tabular
        "${CMAKE_CURRENT_BINARY_DIR}/native-tabular-test-data")

    add_executable(arch_tabular_completion tests/host/eos/TabularCompletionRegression.cpp
        src/core/files/FileFingerprint.cpp
        src/physics/eos/eosdispatch.cpp
        src/physics/eos/sources/Tabular3DEOS.cpp src/physics/eos/sources/Tabular4DEOS.cpp
        src/physics/eos/sources/TabularBaryonSource.cpp src/physics/eos/sources/TabularCompletion.cpp)
    arch_configure_host_test(arch_tabular_completion
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${highfive_SOURCE_DIR}/include" ${HDF5_INCLUDE_DIRS})
    target_link_libraries(arch_tabular_completion PRIVATE arch_build_contract
        ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
    add_test(NAME tabular_component_completion COMMAND arch_tabular_completion
        "${CMAKE_CURRENT_BINARY_DIR}/component-completion-test-data"
        "${CMAKE_CURRENT_SOURCE_DIR}/EOS_toolkit/tables/helmholtz/helm_table.dat")
    set_tests_properties(tabular_component_completion PROPERTIES
        FIXTURES_SETUP tabular_completion_tables)

    # Real author tables are an explicit scientific sample, not an optional
    # skipped test in the default regression inventory.
    add_executable(arch_baryon_eos tests/host/eos/BaryonEosRegression.cpp
        src/core/files/FileFingerprint.cpp src/physics/eos/eosdispatch.cpp
        src/physics/eos/sources/Tabular3DEOS.cpp src/physics/eos/sources/Tabular4DEOS.cpp
        src/physics/eos/sources/TabularBaryonSource.cpp src/physics/eos/sources/TabularCompletion.cpp)
    arch_configure_host_test(arch_baryon_eos "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${highfive_SOURCE_DIR}/include" ${HDF5_INCLUDE_DIRS})
    target_link_libraries(arch_baryon_eos PRIVATE arch_build_contract
        ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})

    if(ARCH_KLU_TARGET)
        add_executable(sparse_klu_regression tests/host/numerics/SparseKLURegression.cpp)
        arch_configure_host_test(sparse_klu_regression)
        target_link_libraries(sparse_klu_regression PRIVATE arch_build_contract)
        add_test(NAME sparse_klu_161_equations COMMAND sparse_klu_regression)
    endif()
endfunction()

# Observed inputs and real Init sinks, separate from reviewed dimensional evidence.
add_executable(arch_initialization_probe tests/api/inspection/test_initialization_probe.cpp
    src/api/configuration/ParameterMetadata.cpp src/api/inspection/CaseUnitEvidence.cpp)
arch_configure_host_test(arch_initialization_probe)
add_test(NAME initialization_probe COMMAND arch_initialization_probe)
add_test(NAME case_inspection_contract
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/api/inspection/test_case_inspection.py
        $<TARGET_FILE:ARCH> ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(case_inspection_contract PROPERTIES TIMEOUT 600)
add_test(NAME preview_session_contract
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/api/session/test_preview_session.py
        $<TARGET_FILE:ARCH> ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(preview_session_contract PROPERTIES TIMEOUT 600)
add_executable(arch_verified_file_cache tests/api/session/test_verified_file_cache.cpp
    src/core/files/FileFingerprint.cpp src/api/resources/WorkerLimits.cpp)
arch_configure_host_test(arch_verified_file_cache)
add_test(NAME preview_verified_resources COMMAND arch_verified_file_cache
    "${CMAKE_CURRENT_BINARY_DIR}/verified-file-cache-data")
add_executable(arch_initial_sample_cache tests/api/session/test_initial_sample_cache.cpp)
arch_configure_host_test(arch_initial_sample_cache)
add_test(NAME preview_exact_sample_cache COMMAND arch_initial_sample_cache)

# Physical-scale invariance uses independent analytic Euler and linear-system references.
add_executable(arch_low_density tests/host/numerics/test_low_density.cpp)
arch_configure_host_test(arch_low_density)
target_link_libraries(arch_low_density PRIVATE arch_build_contract)
add_test(NAME low_density_math COMMAND arch_low_density)

# P2 is standalone CPU mathematics; no fluid dispatch, HDF5 or CUDA execution.
add_executable(arch_poisson_multigrid tests/host/gravity/test_poisson_multigrid.cpp
    src/numerics/elliptic/CartesianPoisson.cpp
    src/numerics/multigrid/HostMultigrid.cpp src/physics/gravity/UniformGravity.cpp)
arch_configure_host_test(arch_poisson_multigrid)
add_test(NAME poisson_multigrid_contract COMMAND arch_poisson_multigrid contract)
add_test(NAME poisson_multigrid_analytic COMMAND arch_poisson_multigrid analytic)
set_tests_properties(poisson_multigrid_analytic PROPERTIES TIMEOUT 600)

# Composite leaf mathematics is independent of AMR fluid storage and dispatch.
add_executable(arch_composite_poisson tests/host/gravity/test_composite_poisson.cpp
    src/numerics/elliptic/CartesianPoisson.cpp src/numerics/elliptic/CompositePoisson.cpp
    src/numerics/multigrid/HostMultigrid.cpp src/numerics/multigrid/HostCompositeMG.cpp)
arch_configure_host_test(arch_composite_poisson)
add_test(NAME composite_poisson_analytic COMMAND arch_composite_poisson 2)
set_tests_properties(composite_poisson_analytic PROPERTIES TIMEOUT 600)

target_link_libraries(arch_resolved_execution_plan PRIVATE arch_gravity_cpu)
add_executable(arch_self_gravity tests/host/gravity/test_self_gravity.cpp
    src/amr/elliptic/EllipticMeshAdapter.cpp)
arch_configure_host_test(arch_self_gravity)
target_link_libraries(arch_self_gravity PRIVATE arch_gravity_cpu)
add_test(NAME self_gravity_lifecycle COMMAND arch_self_gravity)

add_test(NAME composite_poisson_contract COMMAND arch_composite_poisson contract)

add_test(NAME self_gravity_jeans
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/validation/gravity/run_self_gravity.py
        --arch $<TARGET_FILE:ARCH> --output ${CMAKE_CURRENT_BINARY_DIR}/self-gravity-jeans --quick)
set_tests_properties(self_gravity_jeans PROPERTIES TIMEOUT 180 ENVIRONMENT "OMP_NUM_THREADS=1")
