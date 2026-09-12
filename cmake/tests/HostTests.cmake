# Host contract tests. Included only when BUILD_TESTING is enabled.
# Keep HDF5-dependent regression registration after dependency discovery.

# Shared declarations only; individual tests retain their libraries and macros.
function(arch_configure_host_test target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src" ${ARGN})
endfunction()

# Python tooling contracts.
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME portable_network_generator
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/test_portable_network_generator.py)
set_tests_properties(portable_network_generator PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME cuda_amr_smoke_runner_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/test_smoke_cuda_amr_runtime.py)
add_test(NAME curvilinear_manifest_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/test_curvilinear_manifest.py)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME memory_guard_contract
        COMMAND ${Python3_EXECUTABLE} -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/test_run_memory_guarded.py)
endif()
add_test(NAME backend_validation_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/test_validate_backend_results.py)
set_tests_properties(backend_validation_contract PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME sparse_validation_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/validation/network/test_sparse_validation.py)
add_test(NAME validation_provenance_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/test_validation_provenance.py)
set_tests_properties(validation_provenance_contract PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME runtime_validation_inputs_contract
    COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/test_runtime_validation_inputs.py)
set_tests_properties(runtime_validation_inputs_contract PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Numerical leaves and independent reference authorities.
foreach(contract IN ITEMS physical_constants refinement_indicator_math curvilinear_metrics)
    add_executable(arch_${contract} tests/host/test_${contract}.cpp)
    target_link_libraries(arch_${contract} PRIVATE arch_build_contract)
    add_test(NAME ${contract} COMMAND arch_${contract})
endforeach()
add_executable(arch_compensated_sum tests/host/test_compensated_sum.cpp)
target_compile_features(arch_compensated_sum PRIVATE cxx_std_20)
add_test(NAME compensated_sum COMMAND arch_compensated_sum)
add_executable(arch_sparse_ode_continuation tests/host/test_sparse_ode_continuation.cpp)
target_link_libraries(arch_sparse_ode_continuation PRIVATE arch_build_contract)
add_test(NAME sparse_ode_continuation COMMAND arch_sparse_ode_continuation)
add_executable(arch_checkpoint_conservation_metrics tests/host/test_checkpoint_conservation_metrics.cpp)
target_link_libraries(arch_checkpoint_conservation_metrics PRIVATE arch_build_contract)
add_test(NAME checkpoint_conservation_metrics COMMAND arch_checkpoint_conservation_metrics)
add_executable(arch_burn_mainline_reference
    tests/host/test_burn_mainline_reference.cpp src/core/FileFingerprint.cpp)
target_compile_features(arch_burn_mainline_reference PRIVATE cxx_std_20)
target_compile_definitions(arch_burn_mainline_reference PRIVATE
    ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME burn_mainline_reference COMMAND arch_burn_mainline_reference)
add_executable(arch_generated_nse tests/host/test_generated_nse.cpp)
arch_configure_host_test(arch_generated_nse)
target_link_libraries(arch_generated_nse PRIVATE arch_build_contract)
add_test(NAME generated_nse COMMAND arch_generated_nse)
add_executable(arch_tabular_strict tests/math/test_tabular_strict.cpp)
arch_configure_host_test(arch_tabular_strict)
target_link_libraries(arch_tabular_strict PRIVATE arch_build_contract)
add_test(NAME tabular_strict_math COMMAND arch_tabular_strict)
add_executable(arch_baryon_source tests/host/BaryonSourceRegression.cpp
    src/physics/eos/TabularBaryonSource.cpp)
arch_configure_host_test(arch_baryon_source)
target_link_libraries(arch_baryon_source PRIVATE arch_build_contract)
add_test(NAME baryon_source_format COMMAND arch_baryon_source
    "${CMAKE_CURRENT_BINARY_DIR}/baryon-source-test-data")
add_executable(arch_helm_components tests/host/HelmComponentsRegression.cpp)
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
        add_executable(${reference_target} tests/host/test_generated_nse_network.cpp)
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
add_executable(arch_mainline_authority tests/cuda/test_mainline_authority.cpp)
arch_configure_host_test(arch_mainline_authority)
target_compile_definitions(arch_mainline_authority PRIVATE
    ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(arch_mainline_authority PRIVATE Threads::Threads)
add_test(NAME mainline_authority COMMAND arch_mainline_authority)

# Handles, scheduler state and backend-neutral AMR plans.
foreach(contract IN ITEMS block_handle state_residency)
    add_executable(arch_${contract} tests/host/test_${contract}.cpp)
    arch_configure_host_test(arch_${contract})
    add_test(NAME ${contract} COMMAND arch_${contract})
endforeach()

add_executable(arch_shared_stage_scheduler
    tests/host/test_shared_stage_scheduler.cpp)
arch_configure_host_test(arch_shared_stage_scheduler)
target_compile_definitions(arch_shared_stage_scheduler PRIVATE
    ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME shared_stage_scheduler COMMAND arch_shared_stage_scheduler)

foreach(contract IN ITEMS boundary_plan same_level_exchange_plan
        amr_operation_plans amr_flux_surface_plan topology_transaction)
    add_executable(arch_${contract} tests/host/test_${contract}.cpp)
    arch_configure_host_test(arch_${contract})
    add_test(NAME ${contract} COMMAND arch_${contract})
endforeach()

# Runtime policy resolution and backend resource contracts.
add_executable(arch_resolved_execution_plan
    tests/host/test_resolved_execution_plan.cpp)
arch_configure_host_test(arch_resolved_execution_plan "${ARCH_CUSTOM_REGISTRY_DIR}")
target_link_libraries(arch_resolved_execution_plan PRIVATE
    arch_build_contract)
add_test(NAME resolved_execution_plan COMMAND arch_resolved_execution_plan)

add_executable(arch_runtime_probe_capabilities
    tests/host/test_runtime_probe_and_capabilities.cpp)
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

add_executable(arch_reduction_contract tests/host/test_reduction_contract.cpp)
arch_configure_host_test(arch_reduction_contract "${ARCH_CUSTOM_REGISTRY_DIR}")
target_link_libraries(arch_reduction_contract PRIVATE
    arch_build_contract)
add_test(NAME reduction_contract COMMAND arch_reduction_contract)

add_executable(arch_compute_backend tests/host/test_compute_backend.cpp)
arch_configure_host_test(arch_compute_backend)
add_test(NAME compute_backend COMMAND arch_compute_backend)

add_executable(arch_device_block_store_lifecycle
    tests/host/test_device_block_store_lifecycle.cpp)
arch_configure_host_test(arch_device_block_store_lifecycle)
target_link_libraries(arch_device_block_store_lifecycle PRIVATE
    arch_build_contract)
add_test(NAME device_block_store_lifecycle
    COMMAND arch_device_block_store_lifecycle)

# These targets require HDF5/HighFive and the resolved KLU provider.
function(arch_register_io_regression_tests)
    add_executable(arch_checkpoint_compatibility
        tests/host/test_checkpoint_compatibility.cpp
        src/core/FileFingerprint.cpp
        src/physics/eos/eosdispatch.cpp
        src/physics/eos/TabularBaryonSource.cpp
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
        tests/host/TabularEOSRegression.cpp
        src/core/FileFingerprint.cpp
        src/physics/eos/Tabular3DEOS.cpp
        src/physics/eos/Tabular4DEOS.cpp
        src/physics/eos/eosdispatch.cpp
        src/physics/eos/TabularBaryonSource.cpp
        src/physics/eos/TabularCompletion.cpp)
    arch_configure_host_test(tabular_eos_regression
        "${highfive_SOURCE_DIR}/include"
        ${HDF5_INCLUDE_DIRS})
    target_link_libraries(tabular_eos_regression PRIVATE
        ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
    add_test(NAME tabular_eos_ideal_gas
        COMMAND tabular_eos_regression
                "${CMAKE_CURRENT_BINARY_DIR}/tabular-eos-test-data")

    add_executable(arch_native_tabular
        tests/host/NativeTabularRegression.cpp
        src/core/FileFingerprint.cpp
        src/physics/eos/Tabular3DEOS.cpp
        src/physics/eos/eosdispatch.cpp
        src/physics/eos/TabularBaryonSource.cpp
        src/physics/eos/TabularCompletion.cpp)
    arch_configure_host_test(arch_native_tabular
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${highfive_SOURCE_DIR}/include" ${HDF5_INCLUDE_DIRS})
    target_link_libraries(arch_native_tabular PRIVATE
        arch_build_contract ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
    add_test(NAME native_tabular_eos COMMAND arch_native_tabular
        "${CMAKE_CURRENT_BINARY_DIR}/native-tabular-test-data")

    add_executable(arch_tabular_completion tests/host/TabularCompletionRegression.cpp
        src/core/FileFingerprint.cpp
        src/physics/eos/eosdispatch.cpp
        src/physics/eos/Tabular3DEOS.cpp src/physics/eos/Tabular4DEOS.cpp
        src/physics/eos/TabularBaryonSource.cpp src/physics/eos/TabularCompletion.cpp)
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
    add_executable(arch_baryon_eos tests/host/BaryonEosRegression.cpp
        src/core/FileFingerprint.cpp src/physics/eos/eosdispatch.cpp
        src/physics/eos/Tabular3DEOS.cpp src/physics/eos/Tabular4DEOS.cpp
        src/physics/eos/TabularBaryonSource.cpp src/physics/eos/TabularCompletion.cpp)
    arch_configure_host_test(arch_baryon_eos "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${highfive_SOURCE_DIR}/include" ${HDF5_INCLUDE_DIRS})
    target_link_libraries(arch_baryon_eos PRIVATE arch_build_contract
        ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})

    if(ARCH_KLU_TARGET)
        add_executable(sparse_klu_regression tests/host/SparseKLURegression.cpp)
        arch_configure_host_test(sparse_klu_regression)
        target_link_libraries(sparse_klu_regression PRIVATE arch_build_contract)
        add_test(NAME sparse_klu_161_equations COMMAND sparse_klu_regression)
    endif()
endfunction()
