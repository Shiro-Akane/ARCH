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

# Enable LTO (Link Time Optimization)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    message(STATUS "[OPT] LTO (Link Time Optimization) Enabled")

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
option(ARCH_ENABLE_CUDA "Build focused CUDA compile probes and later CUDA backend" OFF)

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
    "${ARCH_CUSTOM_REGISTRY_DIR}/CustomNetworks.generated.h")
file(MAKE_DIRECTORY "${ARCH_CUSTOM_REGISTRY_DIR}")
file(WRITE "${ARCH_CUSTOM_REGISTRY_HEADER}" "#pragma once\n")
set(ARCH_CUSTOM_NETWORK_MACRO "#define ARCH_FOR_EACH_CUSTOM_NETWORK(M)")
set(ARCH_CUSTOM_DISCOVERED_IDS "")
set(ARCH_CUSTOM_RESERVED_IDS custom none null ideal helmholtz tabular)
file(GLOB ARCH_CUSTOM_NETWORK_CONFIGS CONFIGURE_DEPENDS
    "${ARCH_CUSTOM_NETWORK_ROOT}/*/network.cmake")
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
    file(APPEND "${ARCH_CUSTOM_REGISTRY_HEADER}"
        "#include \"${ARCH_CUSTOM_NETWORK_HEADER}\"\n")
    string(APPEND ARCH_CUSTOM_NETWORK_MACRO
        " M(\"custom:${ARCH_CUSTOM_NETWORK_ID}\", ${ARCH_CUSTOM_NETWORK_TYPE})")
    list(APPEND ARCH_APPLICATION_SOURCES "${ARCH_CUSTOM_NETWORK_SOURCE}")
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
file(APPEND "${ARCH_CUSTOM_REGISTRY_HEADER}"
    "\n${ARCH_CUSTOM_NETWORK_MACRO}\n")
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
# Runtime probing is not part of the production startup until E3 threads the
# resolved plan through that call chain.  D1 compiles it only in its focused test.
list(FILTER ARCH_DISPATCH_SOURCES EXCLUDE REGEX "/RuntimeProbe\\.cpp$")

# ==============================================================================
# Target Definition & Properties
# ==============================================================================
add_library(arch_solver_dispatch OBJECT ${ARCH_DISPATCH_SOURCES})

add_executable(ARCH
    ${ARCH_APPLICATION_SOURCES}
    $<TARGET_OBJECTS:arch_solver_dispatch>
)

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

    add_executable(arch_resolved_execution_plan
        tests/test_resolved_execution_plan.cpp)
    target_compile_features(arch_resolved_execution_plan PRIVATE cxx_std_20)
    target_include_directories(arch_resolved_execution_plan PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${ARCH_CUSTOM_REGISTRY_DIR}")
    add_test(NAME resolved_execution_plan COMMAND arch_resolved_execution_plan)

    add_executable(arch_runtime_probe_capabilities
        tests/test_runtime_probe_and_capabilities.cpp
        src/driver/dispatch/RuntimeProbe.cpp)
    target_compile_features(arch_runtime_probe_capabilities PRIVATE cxx_std_20)
    target_include_directories(arch_runtime_probe_capabilities PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(arch_runtime_probe_capabilities PRIVATE
        ARCH_D1_TEST_CUDA_BUILD=$<BOOL:${ARCH_ENABLE_CUDA}>)
    if(CMAKE_DL_LIBS)
        target_link_libraries(arch_runtime_probe_capabilities PRIVATE ${CMAKE_DL_LIBS})
    endif()
    add_test(NAME runtime_probe_and_capabilities
        COMMAND arch_runtime_probe_capabilities)

    add_executable(arch_reduction_contract tests/test_reduction_contract.cpp)
    target_compile_features(arch_reduction_contract PRIVATE cxx_std_20)
    target_include_directories(arch_reduction_contract PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${ARCH_CUSTOM_REGISTRY_DIR}")
    add_test(NAME reduction_contract COMMAND arch_reduction_contract)
endif()

if(ARCH_ENABLE_CUDA)
    enable_language(CUDA)
    if(BUILD_TESTING)
        set_property(TARGET arch_runtime_probe_capabilities APPEND PROPERTY
            BUILD_RPATH "${CMAKE_CUDA_IMPLICIT_LINK_DIRECTORIES}")
    endif()

    # Task A owns only this compile probe.  A later task may create one normal
    # arch_cuda_backend STATIC target for production CUDA sources; do not glob
    # .cu files into ARCH or share CUDA OBJECT files across consumers.
    add_executable(arch_cuda_compile_probe tests/cuda/test_cuda_compile_probe.cu)
    target_compile_features(arch_cuda_compile_probe PRIVATE cxx_std_20)
    target_include_directories(arch_cuda_compile_probe PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    set_target_properties(arch_cuda_compile_probe PROPERTIES
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON)
    add_executable(arch_cuda_eos_host_device_parity
        tests/cuda/test_eos_host_device_parity.cu
        src/cuda/microphysics/helm_eos_loader.h
        src/cuda/microphysics/helm_eos_loader.cpp)
    set_source_files_properties(src/cuda/microphysics/helm_eos_loader.cpp
        PROPERTIES LANGUAGE CUDA)
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
    if(BUILD_TESTING)
        add_executable(arch_cuda_hydro_leaf_parity
            tests/cuda/test_hydro_leaf_parity.cu)
        target_compile_features(arch_cuda_hydro_leaf_parity PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_hydro_leaf_parity PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
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
            tests/cuda/test_burn_policy_parity.cu
            src/cuda/microphysics/helm_eos_loader.h
            src/cuda/microphysics/helm_eos_loader.cpp)
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
        add_executable(arch_cuda_diffusion_rkl_parity
            tests/cuda/test_diffusion_rkl_parity.cu
            src/numerics/diffusion/DiffFunction.cpp)
        target_compile_features(arch_cuda_diffusion_rkl_parity PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_diffusion_rkl_parity PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(arch_cuda_diffusion_rkl_parity PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;--fmad=false;--prec-div=true;--prec-sqrt=true;-Xcompiler=-march=native,-ffast-math>)
        set_target_properties(arch_cuda_diffusion_rkl_parity PROPERTIES
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON)
        add_executable(arch_cuda_policy_resolution
            tests/cuda/test_cuda_policy_resolution.cu)
        target_compile_features(arch_cuda_policy_resolution PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_policy_resolution PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(arch_cuda_policy_resolution PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;--fmad=false;--prec-div=true;--prec-sqrt=true>)
        set_target_properties(arch_cuda_policy_resolution PROPERTIES
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON)
        add_test(NAME cuda_compile_probe COMMAND arch_cuda_compile_probe)
        add_test(NAME hydro_leaf_parity COMMAND arch_cuda_hydro_leaf_parity)
        add_test(NAME eos_host_device_parity COMMAND arch_cuda_eos_host_device_parity)
        add_test(NAME network_nse_device COMMAND arch_cuda_network_nse_device)
        add_test(NAME burn_policy_parity_helpers
            COMMAND arch_cuda_burn_policy_parity helpers)
        add_test(NAME diffusion_rkl_parity
            COMMAND arch_cuda_diffusion_rkl_parity)
        add_test(NAME cuda_policy_resolution
            COMMAND arch_cuda_policy_resolution)
        add_test(NAME cuda_reduction_contract
            COMMAND arch_cuda_reduction_contract)
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
    foreach(target ARCH arch_solver_dispatch)
        target_link_libraries(${target} PRIVATE ${ARCH_KLU_TARGET})
        target_compile_definitions(${target} PRIVATE ARCH_HAS_KLU=1)
    endforeach()
    message(STATUS "[DEP] SuiteSparse KLU enabled for sparse reaction networks")
else()
    foreach(target ARCH arch_solver_dispatch)
        target_compile_definitions(${target} PRIVATE ARCH_HAS_KLU=0)
    endforeach()
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
    add_executable(tabular_eos_regression
        tests/TabularEOSRegression.cpp
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
        target_compile_definitions(sparse_klu_regression PRIVATE ARCH_HAS_KLU=1)
        target_link_libraries(sparse_klu_regression PRIVATE ${ARCH_KLU_TARGET})
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
