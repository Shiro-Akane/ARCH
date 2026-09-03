cmake_minimum_required(VERSION 3.22)

# ==============================================================================
# Project Configuration
# Workflow:
# 1. Select language, compiler, and optional acceleration dependencies.
# 2. Collect source modules into the ARCH executable with a single build contract.
# 3. Keep feature toggles consistent with the runtime policies documented in src/.
# ==============================================================================
project(ARCH LANGUAGES C CXX)

# Require C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "===================================================================")
message(STATUS "Configuring ARCH Framework")
message(STATUS "C++ Standard: C++20 Required")
message(STATUS "===================================================================")

# Enable ccache for faster compilation
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "[OPT] Found ccache: ${CCACHE_PROGRAM}")
endif()

# ==============================================================================
# Performance Optimization
# ==============================================================================
# Default to Release mode if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()
message(STATUS "[OPT] Build Type: ${CMAKE_BUILD_TYPE}")

# Optimization flags for Release mode
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -ffast-math -DNDEBUG")

# Enable LTO only for optimized configurations.  Applying IPO globally also
# adds -flto to Debug objects and makes the already template-heavy CUDA build
# and final link consume substantially more memory without a runtime benefit.
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    message(STATUS "[OPT] LTO enabled for optimized configurations")

    # Use lld or mold for memory-efficient linking, critical for LTO memory safety
    find_program(MOLD_LINKER mold)
    find_program(LLD_LINKER lld)
    if(MOLD_LINKER)
        add_link_options("-fuse-ld=mold")
        message(STATUS "[OPT] Using mold linker for LTO")
    elseif(LLD_LINKER)
        add_link_options("-fuse-ld=lld")
        message(STATUS "[OPT] Using lld linker for LTO")
    endif()
endif()

option(ARCH_ENABLE_OPENMP "Enable OpenMP parallelization" ON)
option(ARCH_VERBOSE_BUILD "Print every compiler and linker command" OFF)
option(ARCH_ENABLE_KLU "Enable SuiteSparse KLU for large reaction networks" ON)
option(ARCH_FETCH_SUITESPARSE "Fetch pinned SuiteSparse when KLU is not installed" ON)
set(ARCH_CUSTOM_NETWORKS "" CACHE STRING "Semicolon-separated custom network IDs to compile; empty selects all")
set(ARCH_CUSTOM_NETWORK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/physics/network/custom" CACHE PATH "Custom network package root")
# This seam is deliberately opt-in: a normal CPU configure must neither enable
# CUDA nor discover a toolkit or nvcc.
option(ARCH_ENABLE_CUDA "Build the CUDA execution backend and focused validation targets" OFF)

set(ARCH_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin" CACHE PATH "Directory for ARCH executables from this build tree")
set(CMAKE_VERBOSE_MAKEFILE ${ARCH_VERBOSE_BUILD})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# Source File Collection
# ==============================================================================
set(APP_SRC_DIRS
    "src/core"
    "src/physics"
    "src/numerics"
    "src/io"
    "simulation"
)

set(ARCH_APPLICATION_SOURCES src/main.cpp)
set(ARCH_BUILTIN_CUSTOM_NETWORK_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/src/physics/network/custom")
file(REAL_PATH "${ARCH_BUILTIN_CUSTOM_NETWORK_ROOT}"
    ARCH_BUILTIN_CUSTOM_NETWORK_ROOT_REAL)
file(REAL_PATH "${ARCH_CUSTOM_NETWORK_ROOT}"
    ARCH_CUSTOM_NETWORK_ROOT_REAL)
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" ARCH_PROJECT_ROOT_REAL)
cmake_path(IS_PREFIX ARCH_PROJECT_ROOT_REAL
    "${ARCH_CUSTOM_NETWORK_ROOT_REAL}" NORMALIZE custom_root_inside_project)
cmake_path(IS_PREFIX ARCH_CUSTOM_NETWORK_ROOT_REAL
    "${ARCH_PROJECT_ROOT_REAL}" NORMALIZE custom_root_contains_project)
cmake_path(IS_PREFIX ARCH_BUILTIN_CUSTOM_NETWORK_ROOT_REAL
    "${ARCH_CUSTOM_NETWORK_ROOT_REAL}" NORMALIZE custom_root_inside_builtin_root)
if(custom_root_contains_project OR
   (custom_root_inside_project AND NOT custom_root_inside_builtin_root))
    message(FATAL_ERROR
        "ARCH_CUSTOM_NETWORK_ROOT must not contain the project and, when it is "
        "inside the source tree, must stay under src/physics/network/custom; "
        "use an external directory for audit builds")
endif()
foreach(dir IN LISTS APP_SRC_DIRS)
    file(GLOB_RECURSE dir_srcs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp")
    foreach(candidate_source IN LISTS dir_srcs)
        file(REAL_PATH "${candidate_source}" candidate_source_real)
        cmake_path(IS_PREFIX ARCH_BUILTIN_CUSTOM_NETWORK_ROOT_REAL
            "${candidate_source_real}" NORMALIZE source_in_builtin_custom_root)
        cmake_path(IS_PREFIX ARCH_CUSTOM_NETWORK_ROOT_REAL
            "${candidate_source_real}" NORMALIZE source_in_configured_custom_root)
        if(NOT source_in_builtin_custom_root AND
           NOT source_in_configured_custom_root)
            list(APPEND ARCH_APPLICATION_SOURCES "${candidate_source}")
        endif()
    endforeach()
endforeach()


# Discover generated custom networks without editing C++ dispatch source.
set(ARCH_CUSTOM_REGISTRY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(ARCH_CUSTOM_REGISTRY_HEADER
    "${ARCH_CUSTOM_REGISTRY_DIR}/CustomNetworkRegistry.generated.h")
set(ARCH_CUSTOM_TYPES_HEADER
    "${ARCH_CUSTOM_REGISTRY_DIR}/CustomNetworks.generated.h")
file(MAKE_DIRECTORY "${ARCH_CUSTOM_REGISTRY_DIR}")
set(ARCH_CUSTOM_REGISTRY_CONTENT "#pragma once\n")
set(ARCH_CUSTOM_TYPES_CONTENT
    "#pragma once\n#include \"CustomNetworkRegistry.generated.h\"\n")
set(ARCH_CUSTOM_ALL "#define ARCH_FOR_EACH_CUSTOM_NETWORK(M)")
set(ARCH_CUSTOM_CPU_ONLY
    "#define ARCH_FOR_EACH_CPU_ONLY_CUSTOM_NETWORK(M)")
set(ARCH_CUSTOM_CUDA "#define ARCH_FOR_EACH_CUDA_CUSTOM_NETWORK(M)")
set(ARCH_CUSTOM_DISCOVERED_IDS "")
set(ARCH_CUSTOM_DISCOVERY_COUNT 0)
set(ARCH_CUSTOM_COUNT 0)
set(ARCH_CUSTOM_RESERVED_IDS custom none null ideal helmholtz tabular)
file(GLOB ARCH_CUSTOM_NETWORK_CONFIGS CONFIGURE_DEPENDS
    "${ARCH_CUSTOM_NETWORK_ROOT}/*/network.cmake")
list(SORT ARCH_CUSTOM_NETWORK_CONFIGS)
foreach(custom_config IN LISTS ARCH_CUSTOM_NETWORK_CONFIGS)
    unset(ARCH_CUSTOM_NETWORK_ID)
    unset(ARCH_CUSTOM_NETWORK_TYPE)
    unset(ARCH_CUSTOM_NETWORK_HEADER)
    unset(ARCH_CUSTOM_NETWORK_SOURCE)
    include("${custom_config}")
    if(NOT ARCH_CUSTOM_NETWORK_ID OR NOT ARCH_CUSTOM_NETWORK_TYPE
       OR NOT EXISTS "${ARCH_CUSTOM_NETWORK_HEADER}"
       OR NOT EXISTS "${ARCH_CUSTOM_NETWORK_SOURCE}")
        message(FATAL_ERROR "Invalid generated custom network package: ${custom_config}")
    endif()

    get_filename_component(custom_dir "${custom_config}" DIRECTORY)
    get_filename_component(custom_folder "${custom_dir}" NAME)
    set(custom_manifest "${custom_dir}/manifest.json")
    if(NOT EXISTS "${custom_manifest}")
        message(FATAL_ERROR
            "Generated custom network is missing manifest.json: ${custom_config}")
    endif()
    file(READ "${custom_manifest}" custom_manifest_json)
    string(JSON custom_manifest_schema ERROR_VARIABLE custom_schema_error
        GET "${custom_manifest_json}" schema_version)
    string(JSON custom_generator_version ERROR_VARIABLE custom_generator_error
        GET "${custom_manifest_json}" generator_version)
    string(JSON custom_manifest_id ERROR_VARIABLE custom_id_error
        GET "${custom_manifest_json}" network_id)
    if(NOT custom_schema_error STREQUAL "NOTFOUND"
       OR NOT custom_generator_error STREQUAL "NOTFOUND"
       OR NOT custom_id_error STREQUAL "NOTFOUND"
       OR NOT custom_manifest_schema EQUAL 1
       OR custom_generator_version LESS 3
       OR NOT custom_manifest_id STREQUAL ARCH_CUSTOM_NETWORK_ID)
        message(FATAL_ERROR
            "Unsupported or inconsistent custom network manifest: ${custom_manifest}")
    endif()
    list(FIND ARCH_CUSTOM_RESERVED_IDS "${ARCH_CUSTOM_NETWORK_ID}"
        custom_reserved_index)
    string(LENGTH "${ARCH_CUSTOM_NETWORK_ID}" custom_id_length)
    if(NOT ARCH_CUSTOM_NETWORK_ID MATCHES "^[a-z]"
       OR ARCH_CUSTOM_NETWORK_ID MATCHES "[^a-z0-9_]"
       OR custom_id_length GREATER 48
       OR ARCH_CUSTOM_NETWORK_ID MATCHES "^(aprox|iso)"
       OR NOT custom_reserved_index EQUAL -1
       OR NOT custom_folder STREQUAL ARCH_CUSTOM_NETWORK_ID)
        message(FATAL_ERROR
            "Unsafe custom network ID/folder contract in ${custom_config}")
    endif()
    list(FIND ARCH_CUSTOM_DISCOVERED_IDS
        "${ARCH_CUSTOM_NETWORK_ID}" duplicate_custom_index)
    if(NOT duplicate_custom_index EQUAL -1)
        message(FATAL_ERROR
            "Duplicate custom network ID: ${ARCH_CUSTOM_NETWORK_ID}")
    endif()
    list(APPEND ARCH_CUSTOM_DISCOVERED_IDS "${ARCH_CUSTOM_NETWORK_ID}")
    math(EXPR custom_numeric_id "1024 + ${ARCH_CUSTOM_DISCOVERY_COUNT}")
    math(EXPR ARCH_CUSTOM_DISCOVERY_COUNT
        "${ARCH_CUSTOM_DISCOVERY_COUNT} + 1")

    file(REAL_PATH "${custom_dir}" custom_dir_real)
    file(REAL_PATH "${ARCH_CUSTOM_NETWORK_HEADER}" custom_header_real)
    file(REAL_PATH "${ARCH_CUSTOM_NETWORK_SOURCE}" custom_source_real)
    cmake_path(IS_PREFIX custom_dir_real "${custom_header_real}"
        NORMALIZE custom_header_inside)
    cmake_path(IS_PREFIX custom_dir_real "${custom_source_real}"
        NORMALIZE custom_source_inside)
    if(NOT custom_header_inside OR NOT custom_source_inside)
        message(FATAL_ERROR
            "Custom network source escaped its package: ${custom_config}")
    endif()

    if(ARCH_CUSTOM_NETWORKS)
        list(FIND ARCH_CUSTOM_NETWORKS "${ARCH_CUSTOM_NETWORK_ID}" custom_index)
        if(custom_index EQUAL -1)
            continue()
        endif()
    endif()
    string(APPEND ARCH_CUSTOM_TYPES_CONTENT
        "#include \"${ARCH_CUSTOM_NETWORK_HEADER}\"\n")
    set(custom_token "Custom_${ARCH_CUSTOM_NETWORK_ID}")
    set(custom_entry
        "M(${custom_token}, ${custom_numeric_id}, \"custom:${ARCH_CUSTOM_NETWORK_ID}\", ${ARCH_CUSTOM_NETWORK_TYPE})")
    string(APPEND ARCH_CUSTOM_ALL " \\")
    string(APPEND ARCH_CUSTOM_ALL "\n    ${custom_entry}")
    # Generator-v3 adapters intentionally use host-only containers in their
    # Jacobian bridge.  They remain CPU-only until a future manifest version
    # explicitly certifies a device binding.
    string(APPEND ARCH_CUSTOM_CPU_ONLY " \\")
    string(APPEND ARCH_CUSTOM_CPU_ONLY "\n    ${custom_entry}")
    list(APPEND ARCH_APPLICATION_SOURCES "${ARCH_CUSTOM_NETWORK_SOURCE}")
    math(EXPR ARCH_CUSTOM_COUNT "${ARCH_CUSTOM_COUNT} + 1")
    message(STATUS "[NET] Registered custom:${ARCH_CUSTOM_NETWORK_ID}")
endforeach()
foreach(requested_custom_id IN LISTS ARCH_CUSTOM_NETWORKS)
    list(FIND ARCH_CUSTOM_DISCOVERED_IDS
        "${requested_custom_id}" requested_custom_index)
    if(requested_custom_index EQUAL -1)
        message(FATAL_ERROR
            "ARCH_CUSTOM_NETWORKS requested an unknown ID: ${requested_custom_id}")
    endif()
endforeach()
string(APPEND ARCH_CUSTOM_REGISTRY_CONTENT
    "#define ARCH_CUSTOM_NETWORK_COUNT ${ARCH_CUSTOM_COUNT}\n"
    "#define ARCH_CUSTOM_CPU_ONLY_NETWORK_COUNT ${ARCH_CUSTOM_COUNT}\n"
    "#define ARCH_CUSTOM_CUDA_NETWORK_COUNT 0\n"
    "${ARCH_CUSTOM_ALL}\n${ARCH_CUSTOM_CPU_ONLY}\n${ARCH_CUSTOM_CUDA}\n")
file(CONFIGURE OUTPUT "${ARCH_CUSTOM_REGISTRY_HEADER}"
    CONTENT "${ARCH_CUSTOM_REGISTRY_CONTENT}" @ONLY NEWLINE_STYLE UNIX)
file(CONFIGURE OUTPUT "${ARCH_CUSTOM_TYPES_HEADER}"
    CONTENT "${ARCH_CUSTOM_TYPES_CONTENT}" @ONLY NEWLINE_STYLE UNIX)
list(REMOVE_DUPLICATES ARCH_APPLICATION_SOURCES)

# Dispatch sources (isolated in its own object target to manage template instantiation)
set(DISPATCH_SRC_DIRS
    "src/driver/dispatch"
)
set(ARCH_DISPATCH_SOURCES src/driver/SolverDispatch.cpp)
foreach(dir IN LISTS DISPATCH_SRC_DIRS)
    file(GLOB dir_srcs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp")
    list(APPEND ARCH_DISPATCH_SOURCES ${dir_srcs})
endforeach()
# DiffFunction is a shared Host authority used by both the CPU driver and the
# production CUDA backend. Compile it once as a narrow math leaf rather than
# changing its language or duplicating it in CUDA targets.
list(FILTER ARCH_APPLICATION_SOURCES EXCLUDE REGEX
    "/src/numerics/diffusion/DiffFunction\\.cpp$")
# ==============================================================================
# Target Definition & Properties
# ==============================================================================
add_library(arch_build_contract INTERFACE)
target_compile_features(arch_build_contract INTERFACE cxx_std_20)
target_include_directories(arch_build_contract INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/cases
    ${ARCH_CUSTOM_REGISTRY_DIR})
target_compile_definitions(arch_build_contract INTERFACE
    ARCH_CUDA_BUILD_ENABLED=$<BOOL:${ARCH_ENABLE_CUDA}>)
# Every ARCH target declared below consumes headers whose policy layout depends
# on this generated registry and the build-feature macros populated later.
# Keep one directory-wide contract instead of maintaining a fragile target list.
link_libraries(arch_build_contract)

add_library(arch_diffusion_math STATIC
    src/numerics/diffusion/DiffFunction.cpp)
target_link_libraries(arch_diffusion_math PUBLIC arch_build_contract)

add_library(arch_solver_dispatch STATIC ${ARCH_DISPATCH_SOURCES})
target_link_libraries(arch_solver_dispatch
    PUBLIC arch_build_contract
    PRIVATE arch_diffusion_math)

add_executable(ARCH ${ARCH_APPLICATION_SOURCES})
target_link_libraries(ARCH PRIVATE arch_solver_dispatch)
if(CMAKE_DL_LIBS)
    target_link_libraries(arch_solver_dispatch PRIVATE ${CMAKE_DL_LIBS})
endif()

foreach(target ARCH arch_solver_dispatch)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/cases
        ${ARCH_CUSTOM_REGISTRY_DIR}
    )
endforeach()

# PCH covers heavy STL/OpenMP headers shared by every dispatch TU
target_precompile_headers(arch_solver_dispatch PRIVATE
    <vector> <string> <memory> <map> <iostream> <cmath> <omp.h>
    <sstream> <stdexcept> <algorithm> <iomanip> <array>
)

# ==============================================================================
# Dispatch Target: Memory-aware compile flags
# ==============================================================================
# Each Dispatch_*.cpp TU expands to ~120k preprocessed lines and instantiates
# a large template matrix (TimeIntegrator × Flux × Recon × EOS × Burner).
# At -O3, each TU can consume >4 GB of RAM; with -j8 this causes WSL OOM.
# Using -O1 here drops per-TU RSS to ~1 GB while keeping correctness.
# The hot inner loops in these TUs are inlined at call sites anyway.
# LTO is also disabled for this target to avoid the linker memory spike.
target_compile_options(arch_solver_dispatch PRIVATE
    $<$<CONFIG:Release>:-O1>           # Intentionally lower than main -O3 flags
    -fno-inline-functions-called-once  # Prevents explosive inline expansion
)
# Disable LTO for this target at the CMake level (overrides CMAKE_INTERPROCEDURAL_OPTIMIZATION)
# LTO would force the linker to process huge bitcode files for the entire template matrix.
set_target_properties(arch_solver_dispatch PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION FALSE
)

set_target_properties(ARCH PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${ARCH_RUNTIME_OUTPUT_DIRECTORY}")

# Focused latest-main authority coverage intentionally stays independent from
# ARCH and its HDF5 linkage so it can run under host sanitizers.
include(CTest)
if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME backend_validation_contract
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_validate_backend_results.py)
    set_tests_properties(backend_validation_contract PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(arch_mainline_authority tests/cuda/test_mainline_authority.cpp)
    target_compile_features(arch_mainline_authority PRIVATE cxx_std_20)
    target_include_directories(arch_mainline_authority PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(arch_mainline_authority PRIVATE
        ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(arch_mainline_authority PRIVATE Threads::Threads)
    add_test(NAME mainline_authority COMMAND arch_mainline_authority)

    add_executable(arch_block_handle tests/test_block_handle.cpp)
    target_compile_features(arch_block_handle PRIVATE cxx_std_20)
    target_include_directories(arch_block_handle PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME block_handle COMMAND arch_block_handle)

    add_executable(arch_state_residency tests/test_state_residency.cpp)
    target_compile_features(arch_state_residency PRIVATE cxx_std_20)
    target_include_directories(arch_state_residency PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME state_residency COMMAND arch_state_residency)

    add_executable(arch_shared_stage_scheduler
        tests/test_shared_stage_scheduler.cpp)
    target_compile_features(arch_shared_stage_scheduler PRIVATE cxx_std_20)
    target_include_directories(arch_shared_stage_scheduler PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(arch_shared_stage_scheduler PRIVATE
        ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME shared_stage_scheduler COMMAND arch_shared_stage_scheduler)

    add_executable(arch_boundary_plan tests/test_boundary_plan.cpp)
    target_compile_features(arch_boundary_plan PRIVATE cxx_std_20)
    target_include_directories(arch_boundary_plan PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME boundary_plan COMMAND arch_boundary_plan)

    add_executable(arch_same_level_exchange_plan
        tests/test_same_level_exchange_plan.cpp)
    target_compile_features(arch_same_level_exchange_plan PRIVATE cxx_std_20)
    target_include_directories(arch_same_level_exchange_plan PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME same_level_exchange_plan
        COMMAND arch_same_level_exchange_plan)

    add_executable(arch_amr_operation_plans
        tests/test_amr_operation_plans.cpp)
    target_compile_features(arch_amr_operation_plans PRIVATE cxx_std_20)
    target_include_directories(arch_amr_operation_plans PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME amr_operation_plans COMMAND arch_amr_operation_plans)

    add_executable(arch_amr_flux_surface_plan
        tests/test_amr_flux_surface_plan.cpp)
    target_compile_features(arch_amr_flux_surface_plan PRIVATE cxx_std_20)
    target_include_directories(arch_amr_flux_surface_plan PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME amr_flux_surface_plan COMMAND arch_amr_flux_surface_plan)

    add_executable(arch_topology_transaction
        tests/test_topology_transaction.cpp)
    target_compile_features(arch_topology_transaction PRIVATE cxx_std_20)
    target_include_directories(arch_topology_transaction PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME topology_transaction COMMAND arch_topology_transaction)

    add_executable(arch_resolved_execution_plan
        tests/test_resolved_execution_plan.cpp)
    target_compile_features(arch_resolved_execution_plan PRIVATE cxx_std_20)
    target_include_directories(arch_resolved_execution_plan PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${ARCH_CUSTOM_REGISTRY_DIR}")
    target_link_libraries(arch_resolved_execution_plan PRIVATE
        arch_build_contract)
    add_test(NAME resolved_execution_plan COMMAND arch_resolved_execution_plan)

    add_executable(arch_runtime_probe_capabilities
        tests/test_runtime_probe_and_capabilities.cpp)
    target_compile_features(arch_runtime_probe_capabilities PRIVATE cxx_std_20)
    target_include_directories(arch_runtime_probe_capabilities PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(arch_runtime_probe_capabilities PRIVATE
        ARCH_D1_TEST_CUDA_BUILD=$<BOOL:${ARCH_ENABLE_CUDA}>)
    if(CMAKE_DL_LIBS)
        target_link_libraries(arch_runtime_probe_capabilities PRIVATE ${CMAKE_DL_LIBS})
    endif()
    target_link_libraries(arch_runtime_probe_capabilities PRIVATE
        arch_solver_dispatch)
    add_test(NAME runtime_probe_and_capabilities
        COMMAND arch_runtime_probe_capabilities)

    add_executable(arch_reduction_contract tests/test_reduction_contract.cpp)
    target_compile_features(arch_reduction_contract PRIVATE cxx_std_20)
    target_include_directories(arch_reduction_contract PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${ARCH_CUSTOM_REGISTRY_DIR}")
    target_link_libraries(arch_reduction_contract PRIVATE
        arch_build_contract)
    add_test(NAME reduction_contract COMMAND arch_reduction_contract)

    add_executable(arch_compute_backend tests/test_compute_backend.cpp)
    target_compile_features(arch_compute_backend PRIVATE cxx_std_20)
    target_include_directories(arch_compute_backend PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME compute_backend COMMAND arch_compute_backend)

    add_executable(arch_device_block_store_lifecycle
        tests/test_device_block_store_lifecycle.cpp)
    target_compile_features(arch_device_block_store_lifecycle PRIVATE cxx_std_20)
    target_include_directories(arch_device_block_store_lifecycle PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(arch_device_block_store_lifecycle PRIVATE
        arch_build_contract)
    add_test(NAME device_block_store_lifecycle
        COMMAND arch_device_block_store_lifecycle)
endif()

if(ARCH_ENABLE_CUDA)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)

    # ccache 4.x understands NVCC dependency/output modes.  Apply the same
    # launcher used by Host C++ to the split CUDA matrix so a small dispatcher
    # or header edit does not force every network/EOS object through NVCC
    # again.  This stays CUDA-opt-in because the language is enabled above.
    if(CCACHE_PROGRAM)
        set(CMAKE_CUDA_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "[OPT] CUDA compiler cache enabled: ${CCACHE_PROGRAM}")
    endif()

    # Ninja may otherwise start every independent object edge even when its
    # logical target participates in an add_dependencies chain.  Put all CUDA
    # backend frontends (NVCC and the table-heavy Host owners) in one named
    # compile pool.  `cmake --build ... --parallel 6` can still use its other
    # five jobs for the lightweight graph, while this lane has a hard peak of
    # one compiler process.  Non-Ninja generators retain the target ordering
    # below and simply ignore this optional scheduling refinement.
    if(CMAKE_GENERATOR MATCHES "Ninja")
        set_property(GLOBAL APPEND PROPERTY JOB_POOLS arch_cuda_heavy=1)
        set(ARCH_CUDA_HEAVY_JOB_POOL arch_cuda_heavy)
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

    # Put every NVCC frontend in one serial lane.  Burn/EOS, hydro, and
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
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;--fmad=false;--prec-div=true;--prec-sqrt=true;-Xcompiler=-march=native,-ffast-math>)
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
        src/cuda/runtime/CudaBackendBurnIdeal.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_burn_helm
        src/cuda/runtime/CudaBackendBurnHelm.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular3d
        src/cuda/runtime/CudaBackendBurnTabular3D.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular3d_aprox13
        src/cuda/runtime/CudaBackendBurnTabular3DAprox13.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular3d_aprox19
        src/cuda/runtime/CudaBackendBurnTabular3DAprox19.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular3d_aprox21
        src/cuda/runtime/CudaBackendBurnTabular3DAprox21.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular3d_iso7
        src/cuda/runtime/CudaBackendBurnTabular3DIso7.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular4d
        src/cuda/runtime/CudaBackendBurnTabular4D.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular4d_aprox13
        src/cuda/runtime/CudaBackendBurnTabular4DAprox13.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular4d_aprox19
        src/cuda/runtime/CudaBackendBurnTabular4DAprox19.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular4d_aprox21
        src/cuda/runtime/CudaBackendBurnTabular4DAprox21.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_burn_tabular4d_iso7
        src/cuda/runtime/CudaBackendBurnTabular4DIso7.cu)
    # Hydro owns one shared template implementation and one canonical source
    # owner per EOS.  Heavy tabular interpolation call boundaries keep NVCC
    # Debug frontend memory bounded without duplicating physics or routes.
    arch_configure_cuda_backend_object(arch_cuda_backend_hydro_ideal
        src/cuda/runtime/CudaBackendHydroIdeal.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_hydro_helm
        src/cuda/runtime/CudaBackendHydroHelm.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_hydro_tabular3
        src/cuda/runtime/CudaBackendHydroTabular3.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_hydro_tabular4
        src/cuda/runtime/CudaBackendHydroTabular4.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_diffusion
        src/cuda/runtime/CudaBackendDiffusion.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_exchange
        src/cuda/runtime/CudaBackendExchange.cu)
    arch_configure_cuda_backend_object(arch_cuda_backend_amr_flux
        src/cuda/runtime/CudaBackendAmrFlux.cu)
    add_dependencies(arch_cuda_backend_burn_helm
        arch_cuda_backend_burn_ideal)
    add_dependencies(arch_cuda_backend_burn_tabular3d
        arch_cuda_backend_burn_helm)
    add_dependencies(arch_cuda_backend_burn_tabular3d_aprox13
        arch_cuda_backend_burn_tabular3d)
    add_dependencies(arch_cuda_backend_burn_tabular3d_aprox19
        arch_cuda_backend_burn_tabular3d_aprox13)
    add_dependencies(arch_cuda_backend_burn_tabular3d_aprox21
        arch_cuda_backend_burn_tabular3d_aprox19)
    add_dependencies(arch_cuda_backend_burn_tabular3d_iso7
        arch_cuda_backend_burn_tabular3d_aprox21)
    add_dependencies(arch_cuda_backend_burn_tabular4d
        arch_cuda_backend_burn_tabular3d_iso7)
    add_dependencies(arch_cuda_backend_burn_tabular4d_aprox13
        arch_cuda_backend_burn_tabular4d)
    add_dependencies(arch_cuda_backend_burn_tabular4d_aprox19
        arch_cuda_backend_burn_tabular4d_aprox13)
    add_dependencies(arch_cuda_backend_burn_tabular4d_aprox21
        arch_cuda_backend_burn_tabular4d_aprox19)
    add_dependencies(arch_cuda_backend_burn_tabular4d_iso7
        arch_cuda_backend_burn_tabular4d_aprox21)
    # The burn and hydro instantiations each approach the memory ceiling of an
    # 8 GiB WSL guest.  Keep every heavy NVCC unit in one lane so callers may
    # still use --parallel 6 for the split host-control work without running
    # two template-heavy device compilations at the same time.
    add_dependencies(arch_cuda_backend_hydro_ideal
        arch_cuda_backend_burn_tabular4d_iso7)
    add_dependencies(arch_cuda_backend_hydro_helm
        arch_cuda_backend_hydro_ideal)
    add_dependencies(arch_cuda_backend_hydro_tabular3
        arch_cuda_backend_hydro_helm)
    add_dependencies(arch_cuda_backend_hydro_tabular4
        arch_cuda_backend_hydro_tabular3)
    add_dependencies(arch_cuda_backend_diffusion
        arch_cuda_backend_hydro_tabular4)
    add_dependencies(arch_cuda_backend_exchange
        arch_cuda_backend_diffusion)
    add_dependencies(arch_cuda_backend_amr_flux
        arch_cuda_backend_exchange)

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
        src/cuda/runtime/CudaBackendResources.cpp -g1)
    arch_configure_cuda_host_object(arch_cuda_backend_core
        src/cuda/runtime/CudaBackendCore.cpp -g1)
    arch_configure_cuda_host_object(arch_cuda_backend_factory
        src/cuda/runtime/CudaBackendFactory.cpp -g1)
    arch_configure_cuda_host_object(arch_cuda_backend_hydro_control
        src/cuda/runtime/CudaBackendHydroControl.cpp -g1)
    arch_configure_cuda_host_object(arch_cuda_backend_microphysics_control
        src/cuda/runtime/CudaBackendMicrophysicsControl.cpp -g1)
    arch_configure_cuda_host_object(arch_cuda_backend_store
        src/cuda/runtime/CudaBackendStore.cpp -g1)

    add_dependencies(arch_cuda_backend_eos_species
        arch_cuda_backend_eos_utils)
    add_dependencies(arch_cuda_backend_eos_helm
        arch_cuda_backend_eos_species)
    add_dependencies(arch_cuda_backend_eos_tabular3
        arch_cuda_backend_eos_helm)
    add_dependencies(arch_cuda_backend_eos_tabular4
        arch_cuda_backend_eos_tabular3)
    add_dependencies(arch_cuda_backend_resources
        arch_cuda_backend_eos_tabular4)
    add_dependencies(arch_cuda_backend_core
        arch_cuda_backend_resources)
    add_dependencies(arch_cuda_backend_factory
        arch_cuda_backend_core)
    add_dependencies(arch_cuda_backend_hydro_control
        arch_cuda_backend_factory)
    add_dependencies(arch_cuda_backend_microphysics_control
        arch_cuda_backend_hydro_control)
    add_dependencies(arch_cuda_backend_store
        arch_cuda_backend_microphysics_control)

    # Finish the inexpensive host-control phase before entering the serial
    # device-template lane.  A clean `--parallel 6` build otherwise overlaps
    # one multi-gigabyte NVCC frontend with five LTO C++ frontends and can
    # exhaust an 8 GiB WSL guest even though the NVCC targets are serialized.
    add_dependencies(arch_cuda_backend_burn_ideal
        arch_cuda_backend_store)

    add_library(arch_cuda_backend STATIC
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
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;--fmad=false;--prec-div=true;--prec-sqrt=true;-Xcompiler=-march=native,-ffast-math>)
    set_target_properties(arch_cuda_backend PROPERTIES
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON
        INTERPROCEDURAL_OPTIMIZATION OFF)
    target_link_libraries(ARCH PRIVATE arch_cuda_backend)
    # A full CUDA build has three explicit phases: backend archive, dispatch
    # archive, and then the application.  The file-backed barriers are stronger
    # than target-level object ordering under Ninja and therefore make
    # `cmake --build ... --parallel 6` safe on an 8 GiB WSL guest.
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

    if(BUILD_TESTING)
    # Focused CUDA targets never enter a production testing-off graph.
    add_executable(arch_cuda_compile_probe tests/cuda/test_cuda_compile_probe.cu)
    target_compile_features(arch_cuda_compile_probe PRIVATE cxx_std_20)
    target_include_directories(arch_cuda_compile_probe PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    set_target_properties(arch_cuda_compile_probe PROPERTIES
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON)
    add_executable(arch_cuda_eos_host_device_parity
        tests/cuda/test_eos_host_device_parity.cu)
    target_compile_features(arch_cuda_eos_host_device_parity PRIVATE cxx_std_20)
    target_include_directories(arch_cuda_eos_host_device_parity PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(arch_cuda_eos_host_device_parity PRIVATE
        ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(arch_cuda_eos_host_device_parity PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;--fmad=false;--prec-div=true;--prec-sqrt=true>)
    set_target_properties(arch_cuda_eos_host_device_parity PROPERTIES
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON)
    target_link_libraries(arch_cuda_eos_host_device_parity PRIVATE
        arch_cuda_backend)
    add_executable(arch_cuda_tabular_free_energy_owner
        tests/cuda/test_tabular_free_energy_owner.cu)
    target_compile_features(arch_cuda_tabular_free_energy_owner PRIVATE
        cxx_std_20)
    target_include_directories(arch_cuda_tabular_free_energy_owner PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_options(arch_cuda_tabular_free_energy_owner PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;--fmad=false;--prec-div=true;--prec-sqrt=true>)
    set_target_properties(arch_cuda_tabular_free_energy_owner PROPERTIES
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON)
    target_link_libraries(arch_cuda_tabular_free_energy_owner PRIVATE
        arch_cuda_backend)
    add_executable(arch_cuda_network_nse_device
        tests/cuda/test_network_nse_device.cu)
    target_compile_features(arch_cuda_network_nse_device PRIVATE cxx_std_20)
    target_include_directories(arch_cuda_network_nse_device PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_options(arch_cuda_network_nse_device PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;--fmad=false;--prec-div=true;--prec-sqrt=true;-Xcompiler=-ffast-math>)
    set_target_properties(arch_cuda_network_nse_device PROPERTIES
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON)
        add_executable(arch_cuda_hydro_leaf_parity
            tests/cuda/test_hydro_leaf_parity.cu)
        target_compile_features(arch_cuda_hydro_leaf_parity PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_hydro_leaf_parity PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(arch_cuda_hydro_leaf_parity PRIVATE
            arch_build_contract)
        target_compile_options(arch_cuda_hydro_leaf_parity PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
        set_target_properties(arch_cuda_hydro_leaf_parity PROPERTIES
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON)
        add_executable(arch_cuda_reduction_contract
            tests/cuda/test_cuda_reduction_contract.cu)
        target_compile_features(arch_cuda_reduction_contract PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_reduction_contract PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(arch_cuda_reduction_contract PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
        set_target_properties(arch_cuda_reduction_contract PROPERTIES
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON)
        add_executable(arch_cuda_burn_policy_parity
            tests/cuda/test_burn_policy_parity.cu)
        target_compile_features(arch_cuda_burn_policy_parity PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_burn_policy_parity PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_definitions(arch_cuda_burn_policy_parity PRIVATE
            ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_compile_options(arch_cuda_burn_policy_parity PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;--fmad=false;--prec-div=true;--prec-sqrt=true;-Xcompiler=-march=native,-fno-inline,-fno-tree-vectorize,-fno-tree-slp-vectorize>)
        set_target_properties(arch_cuda_burn_policy_parity PROPERTIES
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON)
        target_link_libraries(arch_cuda_burn_policy_parity PRIVATE
            arch_cuda_backend)
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
        target_compile_features(arch_cuda_hydro_block PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_hydro_block PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_definitions(arch_cuda_hydro_block PRIVATE
            ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_compile_options(arch_cuda_hydro_block PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;--fmad=false;--prec-div=true;--prec-sqrt=true;-Xcompiler=-march=native,-ffast-math>)
        set_target_properties(arch_cuda_hydro_block PROPERTIES
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON)
        target_link_libraries(arch_cuda_hydro_block PRIVATE
            arch_cuda_backend arch_solver_dispatch)
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
            arch_cuda_backend arch_solver_dispatch)
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
        add_executable(arch_cuda_diffusion_rkl_parity
            tests/cuda/test_diffusion_rkl_parity.cu)
        target_compile_features(arch_cuda_diffusion_rkl_parity PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_diffusion_rkl_parity PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(arch_cuda_diffusion_rkl_parity PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;--fmad=false;--prec-div=true;--prec-sqrt=true;-Xcompiler=-march=native,-ffast-math>)
        set_target_properties(arch_cuda_diffusion_rkl_parity PROPERTIES
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON)
        target_link_libraries(arch_cuda_diffusion_rkl_parity PRIVATE
            arch_cuda_backend)
        add_executable(arch_cuda_boundary_plan_parity
            tests/cuda/test_boundary_plan_parity.cu)
        target_compile_features(arch_cuda_boundary_plan_parity PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_boundary_plan_parity PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(arch_cuda_boundary_plan_parity PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
        set_target_properties(arch_cuda_boundary_plan_parity PROPERTIES
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON)
        add_executable(arch_cuda_policy_resolution
            tests/cuda/test_cuda_policy_resolution.cu)
        target_compile_features(arch_cuda_policy_resolution PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_policy_resolution PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(arch_cuda_policy_resolution PRIVATE
            arch_build_contract)
        target_compile_options(arch_cuda_policy_resolution PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;--fmad=false;--prec-div=true;--prec-sqrt=true>)
        set_target_properties(arch_cuda_policy_resolution PROPERTIES
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON)
        if(ARCH_CUDA_HEAVY_JOB_POOL)
            # Focused CUDA validation is outside the production ARCH target,
            # but a default test build may schedule it at the same time.  Use
            # the same single-slot lane for every CUDA-facing test compiler and
            # linker.  In testing builds ARCH joins that lane as well, avoiding
            # an ordinary application compile alongside a multi-GiB NVCC edge.
            foreach(target IN ITEMS
                    arch_cuda_compile_probe
                    arch_cuda_eos_host_device_parity
                    arch_cuda_tabular_free_energy_owner
                    arch_cuda_network_nse_device
                    arch_cuda_hydro_leaf_parity
                    arch_cuda_reduction_contract
                    arch_cuda_burn_policy_parity
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
        endforeach()
    endif()
endif()

# ==============================================================================
# Dependencies
# ==============================================================================
# 1. OpenMP
if(ARCH_ENABLE_OPENMP)
    find_package(OpenMP REQUIRED COMPONENTS CXX)
    foreach(target ARCH arch_solver_dispatch)
        target_link_libraries(${target} PRIVATE OpenMP::OpenMP_CXX)
        target_compile_definitions(${target} PRIVATE ARCH_OPENMP_ENABLED=1)
    endforeach()
    if(TARGET arch_reduction_contract)
        target_link_libraries(arch_reduction_contract PRIVATE OpenMP::OpenMP_CXX)
        target_compile_definitions(arch_reduction_contract PRIVATE
            ARCH_OPENMP_ENABLED=1)
    endif()
    if(TARGET arch_boundary_plan)
        target_link_libraries(arch_boundary_plan PRIVATE OpenMP::OpenMP_CXX)
        target_compile_definitions(arch_boundary_plan PRIVATE
            ARCH_OPENMP_ENABLED=1)
    endif()
    message(STATUS "[DEP] OpenMP enabled for target ARCH")
else()
    foreach(target ARCH arch_solver_dispatch)
        target_compile_definitions(${target} PRIVATE ARCH_OPENMP_ENABLED=0)
    endforeach()
    if(TARGET arch_reduction_contract)
        target_compile_definitions(arch_reduction_contract PRIVATE
            ARCH_OPENMP_ENABLED=0)
    endif()
    message(STATUS "[DEP] OpenMP disabled for target ARCH")
endif()

# 2. HDF5
# ARCH uses the C++/HL API and OpenMP; it does not issue MPI-HDF5 calls.
# FindHDF5 caches the wrapper-reported parallel flag. A failed wrapper probe can
# change that flag between configure runs and cause a false parallel-mismatch
# warning even when the selected C++ shared libraries are unchanged. Reset only
# this stale cross-configure state; a mismatch found within one search remains
# visible to CMake.
unset(HDF5_IS_PARALLEL)
unset(HDF5_IS_PARALLEL CACHE)
find_package(HDF5 COMPONENTS CXX HL REQUIRED)
if(HDF5_FOUND)
    foreach(target ARCH arch_solver_dispatch)
        target_include_directories(${target} PRIVATE ${HDF5_INCLUDE_DIRS})
        target_link_libraries(${target} PRIVATE ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
    endforeach()
    message(STATUS "[DEP] HDF5 C++ libraries linked successfully")
endif()

# 3. HighFive
include(FetchContent)
FetchContent_Declare(
    highfive
    GIT_REPOSITORY https://github.com/BlueBrain/HighFive.git
    GIT_TAG v2.9.0
)
FetchContent_Populate(highfive)
foreach(target ARCH arch_solver_dispatch)
    target_include_directories(${target} PRIVATE ${highfive_SOURCE_DIR}/include)
endforeach()
# This focused CUDA test reads the production Host checkpoint schema directly.
# Keep it on the same HDF5Writer implementation as CPU restart rather than
# introducing a CUDA-specific reader or duplicating schema logic in the test.
if(TARGET arch_cuda_single_level_validation)
    target_sources(arch_cuda_single_level_validation PRIVATE
        src/io/hdf5/HDF5Writer.cpp)
    target_include_directories(arch_cuda_single_level_validation PRIVATE
        "${highfive_SOURCE_DIR}/include"
        ${HDF5_INCLUDE_DIRS})
    target_link_libraries(arch_cuda_single_level_validation PRIVATE
        ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
endif()
message(STATUS "[DEP] HighFive headers configured via FetchContent")

# 4. SuiteSparse KLU
# Prefer an installed CMake package. The fallback pins an upstream release and
# builds only KLU plus its minimal SuiteSparse dependencies.
set(ARCH_KLU_TARGET "")
if(ARCH_ENABLE_KLU)
    find_package(KLU CONFIG QUIET)
    if(TARGET SuiteSparse::KLU)
        set(ARCH_KLU_TARGET SuiteSparse::KLU)
    elseif(TARGET KLU::KLU)
        set(ARCH_KLU_TARGET KLU::KLU)
    elseif(ARCH_FETCH_SUITESPARSE)
        set(SUITESPARSE_ENABLE_PROJECTS "klu;amd;colamd" CACHE STRING "" FORCE)
        set(KLU_USE_CHOLMOD OFF CACHE BOOL "" FORCE)
        set(SUITESPARSE_USE_CUDA OFF CACHE BOOL "" FORCE)
        set(SUITESPARSE_USE_FORTRAN OFF CACHE BOOL "" FORCE)
        set(SUITESPARSE_REQUIRE_BLAS OFF CACHE BOOL "" FORCE)
        set(BLA_VENDOR Generic CACHE STRING "" FORCE)
        set(SUITESPARSE_USE_OPENMP OFF CACHE BOOL "" FORCE)
        set(SUITESPARSE_USE_PYTHON OFF CACHE BOOL "" FORCE)
        set(SUITESPARSE_DEMOS OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            suitesparse
            GIT_REPOSITORY https://github.com/DrTimothyAldenDavis/SuiteSparse.git
            GIT_TAG v7.13.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(suitesparse)
        set(ARCH_KLU_TARGET SuiteSparse::KLU)
    endif()
endif()

if(ARCH_KLU_TARGET)
    target_compile_definitions(arch_build_contract INTERFACE ARCH_HAS_KLU=1)
    target_link_libraries(arch_build_contract INTERFACE ${ARCH_KLU_TARGET})
    message(STATUS "[DEP] SuiteSparse KLU enabled for sparse reaction networks")
else()
    target_compile_definitions(arch_build_contract INTERFACE ARCH_HAS_KLU=0)
    if(ARCH_ENABLE_KLU)
        message(FATAL_ERROR
            "ARCH_ENABLE_KLU=ON but KLU was not found and ARCH_FETCH_SUITESPARSE=OFF")
    endif()
    message(STATUS "[DEP] SuiteSparse KLU disabled; SparseKLU selection will be unavailable")
endif()


# ==============================================================================
# Regression Tests
# ==============================================================================
include(CTest)
if(BUILD_TESTING)
    add_executable(arch_checkpoint_compatibility
        tests/test_checkpoint_compatibility.cpp
        src/core/FileFingerprint.cpp
        src/io/chk/CheckpointCompatibility.cpp
        src/io/hdf5/HDF5Writer.cpp)
    target_compile_features(arch_checkpoint_compatibility PRIVATE cxx_std_20)
    target_include_directories(arch_checkpoint_compatibility PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${highfive_SOURCE_DIR}/include"
        ${HDF5_INCLUDE_DIRS})
    target_link_libraries(arch_checkpoint_compatibility PRIVATE
        ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
    add_test(NAME checkpoint_compatibility
        COMMAND arch_checkpoint_compatibility
                "${CMAKE_CURRENT_BINARY_DIR}/checkpoint-compatibility-data")

    add_executable(tabular_eos_regression
        tests/TabularEOSRegression.cpp
        src/core/FileFingerprint.cpp
        src/physics/eos/Tabular3DEOS.cpp
        src/physics/eos/Tabular4DEOS.cpp
        src/physics/eos/eosdispatch.cpp)
    target_compile_features(tabular_eos_regression PRIVATE cxx_std_20)
    target_include_directories(tabular_eos_regression PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${highfive_SOURCE_DIR}/include"
        ${HDF5_INCLUDE_DIRS})
    target_link_libraries(tabular_eos_regression PRIVATE
        ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
    add_test(NAME tabular_eos_ideal_gas
        COMMAND tabular_eos_regression
                "${CMAKE_CURRENT_BINARY_DIR}/tabular-eos-test-data")

    if(ARCH_KLU_TARGET)
        add_executable(sparse_klu_regression tests/SparseKLURegression.cpp)
        target_compile_features(sparse_klu_regression PRIVATE cxx_std_20)
        target_include_directories(sparse_klu_regression PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(sparse_klu_regression PRIVATE arch_build_contract)
        add_test(NAME sparse_klu_161_equations COMMAND sparse_klu_regression)
    endif()
endif()

# ==============================================================================
# Runtime Environment Setup & Final Status
# ==============================================================================
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output)

message(STATUS "===================================================================")
message(STATUS "[SUCCESS] ARCH Framework CPU build environment configured successfully!")
message(STATUS "[SUCCESS] Executable : ${ARCH_RUNTIME_OUTPUT_DIRECTORY}/ARCH")
message(STATUS "[SUCCESS] Data Output: ${CMAKE_CURRENT_SOURCE_DIR}/output/")
message(STATUS "===================================================================")
