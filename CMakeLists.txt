cmake_minimum_required(VERSION 3.22)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

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

# Optimization must preserve the shared physics library's ordered reductions,
# finite-value checks, and CPU/device operation boundaries in every build type.
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

# Enable LTO only for optimized configurations.  Applying IPO globally also
# adds -flto to Debug objects and makes the already template-heavy CUDA build
# and final link consume substantially more memory without a runtime benefit.
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
    # Dependencies built in this tree (notably KLU) also carry LTO bytecode.
    # Separate per-language IPO probes cannot diagnose cross-version linking.
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
       AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
        string(REGEX MATCH "^[0-9]+" arch_c_major "${CMAKE_C_COMPILER_VERSION}")
        string(REGEX MATCH "^[0-9]+" arch_cxx_major "${CMAKE_CXX_COMPILER_VERSION}")
        if(NOT arch_c_major STREQUAL arch_cxx_major)
            message(FATAL_ERROR
                "Release LTO requires matching GCC C/C++ major versions. Selected "
                "${CMAKE_C_COMPILER} (${CMAKE_C_COMPILER_VERSION}) and "
                "${CMAKE_CXX_COMPILER} (${CMAKE_CXX_COMPILER_VERSION}). Configure "
                "CMAKE_C_COMPILER and CMAKE_CXX_COMPILER from the same GCC installation; "
                "use that C++ compiler for CMAKE_CUDA_HOST_COMPILER as well.")
        endif()
    endif()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    message(STATUS "[OPT] LTO enabled for optimized configurations")

    # A linker being installed does not establish that it accepts this
    # compiler's LTO objects. Probe the final C/C++ archive-to-executable link.
    include(cmake/SelectIpoLinker.cmake)
    arch_select_ipo_linker(arch_ipo_link_option)
    if(arch_ipo_link_option)
        add_link_options("${arch_ipo_link_option}")
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
set(ARCH_CUDA_DEBUG_PTXAS_OPT_LEVEL "1" CACHE STRING
    "CUDA Debug assembler optimization (0-3); bounds optimizer cost without changing shared physics")
set_property(CACHE ARCH_CUDA_DEBUG_PTXAS_OPT_LEVEL PROPERTY STRINGS 0 1 2 3)
if(NOT ARCH_CUDA_DEBUG_PTXAS_OPT_LEVEL MATCHES "^[0-3]$")
    message(FATAL_ERROR "ARCH_CUDA_DEBUG_PTXAS_OPT_LEVEL must be 0, 1, 2, or 3")
endif()
option(ARCH_ENABLE_CUDSS "Discover cuDSS for the CUDA sparse solver" ON)
set(ARCH_CUDA_HEAVY_COMPILE_JOBS "1" CACHE STRING
    "Concurrent heavy CUDA/dispatch compiles in Ninja; increase only after memory-guarded measurements")
if(NOT ARCH_CUDA_HEAVY_COMPILE_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "ARCH_CUDA_HEAVY_COMPILE_JOBS must be a positive integer")
endif()
set(CUDSS_ROOT "" CACHE PATH "Optional cuDSS installation prefix")

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
set(ARCH_CUSTOM_LAYOUT "#define ARCH_FOR_EACH_CUSTOM_NETWORK_LAYOUT(M)")
set(ARCH_CUSTOM_DISCOVERED_IDS "")
set(ARCH_CUSTOM_DISCOVERY_COUNT 0)
set(ARCH_CUSTOM_COUNT 0)
set(ARCH_CUSTOM_CPU_ONLY_COUNT 0)
set(ARCH_CUSTOM_CUDA_COUNT 0)
set(ARCH_CUSTOM_CUDA_IDS "")
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
    string(JSON custom_species ERROR_VARIABLE custom_species_error
        GET "${custom_manifest_json}" species_count)
    string(JSON custom_auxiliary ERROR_VARIABLE custom_auxiliary_error
        GET "${custom_manifest_json}" auxiliary_equations)
    if(NOT custom_auxiliary_error STREQUAL "NOTFOUND")
        set(custom_auxiliary 0) # Packages predating integrated source states.
    endif()
    if(NOT custom_species_error STREQUAL "NOTFOUND"
       OR NOT custom_species MATCHES "^[1-9][0-9]*$"
       OR NOT custom_auxiliary MATCHES "^[01]$")
        message(FATAL_ERROR "Invalid custom network ODE layout: ${custom_manifest}")
    endif()
    string(APPEND ARCH_CUSTOM_TYPES_CONTENT
        "#include \"${ARCH_CUSTOM_NETWORK_HEADER}\"\n"
        "static_assert(${ARCH_CUSTOM_NETWORK_TYPE}::NUM_SPECIES == ${custom_species}, \"Network species metadata mismatch\");\n"
        "static_assert(${ARCH_CUSTOM_NETWORK_TYPE}::ODE_NEQ == ${custom_species} + 1 + ${custom_auxiliary}, \"Network ODE metadata mismatch\");\n")
    set(custom_token "Custom_${ARCH_CUSTOM_NETWORK_ID}")
    string(APPEND ARCH_CUSTOM_LAYOUT " \\")
    string(APPEND ARCH_CUSTOM_LAYOUT
        "\n    M(${custom_token}, ${custom_species}, ${custom_auxiliary})")
    set(custom_entry
        "M(${custom_token}, ${custom_numeric_id}, \"custom:${ARCH_CUSTOM_NETWORK_ID}\", ${ARCH_CUSTOM_NETWORK_TYPE})")
    string(APPEND ARCH_CUSTOM_ALL " \\")
    string(APPEND ARCH_CUSTOM_ALL "\n    ${custom_entry}")
    # Device registration requires a generated host/device mathematical body,
    # immutable device-accessible tables and a lowered Jacobian structure.
    # Packages without this contract retain a CPU-only execution route.
    string(JSON custom_device_math ERROR_VARIABLE custom_device_error
        GET "${custom_manifest_json}" device_callable_math)
    string(JSON custom_device_type ERROR_VARIABLE custom_device_type_error
        TYPE "${custom_manifest_json}" device_callable_math)
    if(custom_generator_version GREATER_EQUAL 4
       AND custom_device_error STREQUAL "NOTFOUND"
       AND custom_device_type STREQUAL "BOOLEAN" AND custom_device_math)
        string(APPEND ARCH_CUSTOM_CUDA " \\")
        string(APPEND ARCH_CUSTOM_CUDA "\n    ${custom_entry}")
        list(APPEND ARCH_CUSTOM_CUDA_IDS "${ARCH_CUSTOM_NETWORK_ID}")
        set(ARCH_CUSTOM_TYPE_${ARCH_CUSTOM_NETWORK_ID} "${ARCH_CUSTOM_NETWORK_TYPE}")
        set(ARCH_CUSTOM_HEADER_${ARCH_CUSTOM_NETWORK_ID} "${ARCH_CUSTOM_NETWORK_HEADER}")
        set(ARCH_CUSTOM_AUXILIARY_${ARCH_CUSTOM_NETWORK_ID} "${custom_auxiliary}")
        math(EXPR ARCH_CUSTOM_EQUATIONS_${ARCH_CUSTOM_NETWORK_ID}
            "${custom_species} + 1 + ${custom_auxiliary}")
        math(EXPR ARCH_CUSTOM_CUDA_COUNT "${ARCH_CUSTOM_CUDA_COUNT} + 1")
    else()
        string(APPEND ARCH_CUSTOM_CPU_ONLY " \\")
        string(APPEND ARCH_CUSTOM_CPU_ONLY "\n    ${custom_entry}")
        math(EXPR ARCH_CUSTOM_CPU_ONLY_COUNT "${ARCH_CUSTOM_CPU_ONLY_COUNT} + 1")
    endif()
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
    "#define ARCH_CUSTOM_CPU_ONLY_NETWORK_COUNT ${ARCH_CUSTOM_CPU_ONLY_COUNT}\n"
    "#define ARCH_CUSTOM_CUDA_NETWORK_COUNT ${ARCH_CUSTOM_CUDA_COUNT}\n"
    "${ARCH_CUSTOM_ALL}\n${ARCH_CUSTOM_CPU_ONLY}\n${ARCH_CUSTOM_CUDA}\n${ARCH_CUSTOM_LAYOUT}\n")
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
# One floating-point contract for all users of the shared mathematical headers.
# In particular, reassociation can optimize compensated sums back into naive
# sums.  CUDA host compilation must obey the same contract as ordinary C++;
# these flags change compilation semantics, not the mathematical implementation.
target_compile_options(arch_build_contract INTERFACE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fno-fast-math;-ffp-contract=off>
    $<$<AND:$<COMPILE_LANG_AND_ID:CUDA,NVIDIA>,$<CONFIG:Debug>>:-Xptxas=-O${ARCH_CUDA_DEBUG_PTXAS_OPT_LEVEL}>
    $<$<COMPILE_LANG_AND_ID:CUDA,NVIDIA>:--fmad=false;--ftz=false;--prec-div=true;--prec-sqrt=true;-Xcompiler=-fno-fast-math,-ffp-contract=off>)
# Global/toolchain fast-math flags can also cause GCC to link crtfastmath.o,
# enabling process-wide FTZ/DAZ even when individual files compiled strictly.
target_link_options(arch_build_contract INTERFACE
    $<$<LINK_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fno-fast-math;-ffp-contract=off>
    $<$<LINK_LANG_AND_ID:CUDA,NVIDIA>:$<HOST_LINK:-fno-fast-math;-ffp-contract=off>>)
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
# Each Dispatch_*.cpp TU instantiates a large policy matrix. Preserve main's
# reduced frontend optimization here; the numerical and Release IPO contracts
# remain shared. Measure actual compile/link commands and runtime before changing
# this boundary rather than inferring cost from the number of source files.
target_compile_options(arch_solver_dispatch PRIVATE
    $<$<CONFIG:Release>:-O1>           # Intentionally lower than main -O3 flags
    -fno-inline-functions-called-once  # Prevents explosive inline expansion
)
# Preserve the unsuffixed baseline property. Configuration-specific Release /
# RelWithDebInfo IPO above takes precedence when supported, so these optimized
# dispatch objects still carry LTO; this is not a no-LTO compilation boundary.
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
    add_executable(arch_mainline_authority tests/cuda/test_mainline_authority.cpp)
    target_compile_features(arch_mainline_authority PRIVATE cxx_std_20)
    target_include_directories(arch_mainline_authority PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(arch_mainline_authority PRIVATE
        ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(arch_mainline_authority PRIVATE Threads::Threads)
    add_test(NAME mainline_authority COMMAND arch_mainline_authority)

    add_executable(arch_block_handle tests/host/test_block_handle.cpp)
    target_compile_features(arch_block_handle PRIVATE cxx_std_20)
    target_include_directories(arch_block_handle PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME block_handle COMMAND arch_block_handle)

    add_executable(arch_state_residency tests/host/test_state_residency.cpp)
    target_compile_features(arch_state_residency PRIVATE cxx_std_20)
    target_include_directories(arch_state_residency PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME state_residency COMMAND arch_state_residency)

    add_executable(arch_shared_stage_scheduler
        tests/host/test_shared_stage_scheduler.cpp)
    target_compile_features(arch_shared_stage_scheduler PRIVATE cxx_std_20)
    target_include_directories(arch_shared_stage_scheduler PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(arch_shared_stage_scheduler PRIVATE
        ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME shared_stage_scheduler COMMAND arch_shared_stage_scheduler)

    add_executable(arch_boundary_plan tests/host/test_boundary_plan.cpp)
    target_compile_features(arch_boundary_plan PRIVATE cxx_std_20)
    target_include_directories(arch_boundary_plan PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME boundary_plan COMMAND arch_boundary_plan)

    add_executable(arch_same_level_exchange_plan
        tests/host/test_same_level_exchange_plan.cpp)
    target_compile_features(arch_same_level_exchange_plan PRIVATE cxx_std_20)
    target_include_directories(arch_same_level_exchange_plan PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME same_level_exchange_plan
        COMMAND arch_same_level_exchange_plan)

    add_executable(arch_amr_operation_plans
        tests/host/test_amr_operation_plans.cpp)
    target_compile_features(arch_amr_operation_plans PRIVATE cxx_std_20)
    target_include_directories(arch_amr_operation_plans PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME amr_operation_plans COMMAND arch_amr_operation_plans)

    add_executable(arch_amr_flux_surface_plan
        tests/host/test_amr_flux_surface_plan.cpp)
    target_compile_features(arch_amr_flux_surface_plan PRIVATE cxx_std_20)
    target_include_directories(arch_amr_flux_surface_plan PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME amr_flux_surface_plan COMMAND arch_amr_flux_surface_plan)

    add_executable(arch_topology_transaction
        tests/host/test_topology_transaction.cpp)
    target_compile_features(arch_topology_transaction PRIVATE cxx_std_20)
    target_include_directories(arch_topology_transaction PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME topology_transaction COMMAND arch_topology_transaction)

    add_executable(arch_resolved_execution_plan
        tests/host/test_resolved_execution_plan.cpp)
    target_compile_features(arch_resolved_execution_plan PRIVATE cxx_std_20)
    target_include_directories(arch_resolved_execution_plan PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${ARCH_CUSTOM_REGISTRY_DIR}")
    target_link_libraries(arch_resolved_execution_plan PRIVATE
        arch_build_contract)
    add_test(NAME resolved_execution_plan COMMAND arch_resolved_execution_plan)

    add_executable(arch_runtime_probe_capabilities
        tests/host/test_runtime_probe_and_capabilities.cpp)
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

    add_executable(arch_reduction_contract tests/host/test_reduction_contract.cpp)
    target_compile_features(arch_reduction_contract PRIVATE cxx_std_20)
    target_include_directories(arch_reduction_contract PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${ARCH_CUSTOM_REGISTRY_DIR}")
    target_link_libraries(arch_reduction_contract PRIVATE
        arch_build_contract)
    add_test(NAME reduction_contract COMMAND arch_reduction_contract)

    add_executable(arch_compute_backend tests/host/test_compute_backend.cpp)
    target_compile_features(arch_compute_backend PRIVATE cxx_std_20)
    target_include_directories(arch_compute_backend PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME compute_backend COMMAND arch_compute_backend)

    add_executable(arch_device_block_store_lifecycle
        tests/host/test_device_block_store_lifecycle.cpp)
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

    if(BUILD_TESTING)
    # Focused CUDA targets never enter a production testing-off graph.
    foreach(custom_id IN LISTS ARCH_CUSTOM_CUDA_IDS)
        set(custom_test "arch_cuda_generated_math_${custom_id}")
        add_executable(${custom_test} tests/cuda/test_generated_network_math.cu)
        target_compile_options(${custom_test} PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>
            "-include" "${ARCH_CUSTOM_HEADER_${custom_id}}")
        target_compile_definitions(${custom_test} PRIVATE
            ARCH_TEST_NETWORK_TYPE=${ARCH_CUSTOM_TYPE_${custom_id}})
        target_link_libraries(${custom_test} PRIVATE CUDA::cudart)
        set_target_properties(${custom_test} PROPERTIES CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON INTERPROCEDURAL_OPTIMIZATION OFF)
        if(ARCH_CUDA_HEAVY_JOB_POOL)
            set_property(TARGET ${custom_test} PROPERTY JOB_POOL_COMPILE ${ARCH_CUDA_HEAVY_JOB_POOL})
        endif()
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
            target_compile_options(${sparse_test} PRIVATE
                $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>
                "-include" "${ARCH_CUSTOM_HEADER_${custom_id}}")
            target_compile_definitions(${sparse_test} PRIVATE
                ARCH_TEST_NETWORK_TYPE=${ARCH_CUSTOM_TYPE_${custom_id}})
            target_link_libraries(${sparse_test} PRIVATE arch_build_contract arch_cuda_sparse_provider)
            set_target_properties(${sparse_test} PROPERTIES CUDA_STANDARD 20
                CUDA_STANDARD_REQUIRED ON INTERPROCEDURAL_OPTIMIZATION OFF)
            if(ARCH_CUDA_HEAVY_JOB_POOL)
                set_property(TARGET ${sparse_test} PROPERTY JOB_POOL_COMPILE ${ARCH_CUDA_HEAVY_JOB_POOL})
            endif()
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
                target_compile_options(${weak_test} PRIVATE
                    $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>
                    "-include" "${ARCH_CUSTOM_HEADER_${custom_id}}")
                target_compile_definitions(${weak_test} PRIVATE
                    ARCH_TEST_NETWORK_TYPE=${ARCH_CUSTOM_TYPE_${custom_id}})
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
                set_target_properties(${weak_test} PROPERTIES CUDA_STANDARD 20
                    CUDA_STANDARD_REQUIRED ON INTERPROCEDURAL_OPTIMIZATION OFF)
                if(ARCH_CUDA_HEAVY_JOB_POOL)
                    set_property(TARGET ${weak_test} PROPERTY JOB_POOL_COMPILE ${ARCH_CUDA_HEAVY_JOB_POOL})
                endif()
            endforeach()
        endif()
    endforeach()
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
    target_compile_options(arch_cuda_grid_metrics_cache PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
    set_target_properties(arch_cuda_grid_metrics_cache PROPERTIES
        CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON
        INTERPROCEDURAL_OPTIMIZATION OFF)
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
        target_compile_options(arch_cuda_sparse_be_nr_batch PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
        set_target_properties(arch_cuda_sparse_be_nr_batch PROPERTIES
            CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON INTERPROCEDURAL_OPTIMIZATION OFF)
        add_test(NAME cuda_sparse_be_nr_batch COMMAND arch_cuda_sparse_be_nr_batch)
        set_tests_properties(cuda_sparse_be_nr_batch PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(arch_cuda_cudss_sparse_solver tests/cuda/test_cudss_sparse_solver.cpp)
        target_link_libraries(arch_cuda_cudss_sparse_solver PRIVATE arch_cuda_sparse_provider)
        add_test(NAME cuda_cudss_sparse_solver COMMAND arch_cuda_cudss_sparse_solver)
        set_tests_properties(cuda_cudss_sparse_solver PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(arch_cuda_burn_eos_failure tests/cuda/test_burn_eos_failure.cu)
        target_link_libraries(arch_cuda_burn_eos_failure PRIVATE arch_cuda_sparse_provider)
        target_compile_options(arch_cuda_burn_eos_failure PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
        set_target_properties(arch_cuda_burn_eos_failure PROPERTIES
            CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON INTERPROCEDURAL_OPTIMIZATION OFF)
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
    target_compile_options(arch_cuda_regrid_migration PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
    set_target_properties(arch_cuda_regrid_migration PROPERTIES
        CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON INTERPROCEDURAL_OPTIMIZATION OFF)
    add_test(NAME cuda_regrid_migration COMMAND arch_cuda_regrid_migration)
    set_tests_properties(cuda_regrid_migration PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(arch_cuda_curvilinear_geometry_smoke
        tests/cuda/test_curvilinear_geometry_smoke.cu)
    target_link_libraries(arch_cuda_curvilinear_geometry_smoke PRIVATE
        arch_build_contract CUDA::cudart arch_diffusion_math)
    target_compile_options(arch_cuda_curvilinear_geometry_smoke PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
    set_target_properties(arch_cuda_curvilinear_geometry_smoke PROPERTIES
        CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON INTERPROCEDURAL_OPTIMIZATION OFF)
    add_test(NAME cuda_curvilinear_geometry_smoke COMMAND arch_cuda_curvilinear_geometry_smoke)
    set_tests_properties(cuda_curvilinear_geometry_smoke PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(arch_cuda_hydro_eos_failure tests/cuda/test_hydro_eos_failure.cu)
    target_link_libraries(arch_cuda_hydro_eos_failure PRIVATE arch_build_contract CUDA::cudart)
    target_compile_options(arch_cuda_hydro_eos_failure PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
    set_target_properties(arch_cuda_hydro_eos_failure PROPERTIES
        CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON INTERPROCEDURAL_OPTIMIZATION OFF)
    if(ARCH_CUDA_HEAVY_JOB_POOL)
        set_property(TARGET arch_cuda_hydro_eos_failure PROPERTY JOB_POOL_COMPILE ${ARCH_CUDA_HEAVY_JOB_POOL})
    endif()
    add_test(NAME cuda_hydro_eos_failure COMMAND arch_cuda_hydro_eos_failure)
    set_tests_properties(cuda_hydro_eos_failure PROPERTIES SKIP_RETURN_CODE 77)
    # Host-only registry execution compiled by NVCC: catches closure-lowering
    # regressions without instantiating the full Hydro numerical route matrix.
    add_executable(arch_cuda_hydro_dispatch tests/cuda/test_hydro_dispatch.cu)
    target_link_libraries(arch_cuda_hydro_dispatch PRIVATE arch_build_contract CUDA::cudart)
    target_compile_options(arch_cuda_hydro_dispatch PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
    set_target_properties(arch_cuda_hydro_dispatch PROPERTIES
        CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON INTERPROCEDURAL_OPTIMIZATION OFF)
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
    target_compile_options(arch_cuda_compensated_sum PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
    set_target_properties(arch_cuda_compensated_sum PROPERTIES
        CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON
        INTERPROCEDURAL_OPTIMIZATION OFF)
    add_test(NAME cuda_compensated_sum COMMAND arch_cuda_compensated_sum)
    set_tests_properties(cuda_compensated_sum PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(arch_cuda_amr_composition
        tests/cuda/test_cuda_amr_composition.cu)
    target_compile_features(arch_cuda_amr_composition PRIVATE cxx_std_20)
    target_compile_options(arch_cuda_amr_composition PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
    target_link_libraries(arch_cuda_amr_composition PRIVATE CUDA::cudart)
    set_target_properties(arch_cuda_amr_composition PROPERTIES
        CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON
        INTERPROCEDURAL_OPTIMIZATION OFF)
    add_test(NAME cuda_amr_composition COMMAND arch_cuda_amr_composition)
    set_tests_properties(cuda_amr_composition PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(arch_cuda_compile_probe tests/cuda/test_cuda_compile_probe.cu)
    target_compile_features(arch_cuda_compile_probe PRIVATE cxx_std_20)
    target_include_directories(arch_cuda_compile_probe PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_options(arch_cuda_compile_probe PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
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
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
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
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
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
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
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
            tests/cuda/test_burn_policy_parity.cu
            src/core/FileFingerprint.cpp)
        target_compile_features(arch_cuda_burn_policy_parity PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_burn_policy_parity PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_definitions(arch_cuda_burn_policy_parity PRIVATE
            ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_compile_options(arch_cuda_burn_policy_parity PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;-Xcompiler=-march=native,-fno-inline,-fno-tree-vectorize,-fno-tree-slp-vectorize>)
        set_target_properties(arch_cuda_burn_policy_parity PROPERTIES
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON)
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
        target_compile_features(arch_cuda_burn_controller_parity PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_burn_controller_parity PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_definitions(arch_cuda_burn_controller_parity PRIVATE
            ARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_compile_options(arch_cuda_burn_controller_parity PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
        set_target_properties(arch_cuda_burn_controller_parity PROPERTIES
            CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON)
        target_link_libraries(arch_cuda_burn_controller_parity PRIVATE
            arch_cuda_backend_eos_helm arch_cuda_backend_eos_species
            arch_cuda_backend_eos_utils arch_build_contract CUDA::cudart)
        add_test(NAME burn_controller_parity COMMAND arch_cuda_burn_controller_parity)
        set_tests_properties(burn_controller_parity PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(arch_cuda_network_derivative tests/cuda/test_network_derivative.cu)
        target_compile_features(arch_cuda_network_derivative PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_network_derivative PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(arch_cuda_network_derivative PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
        set_target_properties(arch_cuda_network_derivative PROPERTIES
            CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON)
        target_link_libraries(arch_cuda_network_derivative PRIVATE arch_build_contract CUDA::cudart)
        add_test(NAME cuda_network_derivative COMMAND arch_cuda_network_derivative)
        set_tests_properties(cuda_network_derivative PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(arch_cuda_burn_thermal_math tests/cuda/test_burn_thermal_math.cu)
        target_compile_features(arch_cuda_burn_thermal_math PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_burn_thermal_math PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(arch_cuda_burn_thermal_math PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
        set_target_properties(arch_cuda_burn_thermal_math PROPERTIES
            CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON)
        target_link_libraries(arch_cuda_burn_thermal_math PRIVATE arch_build_contract CUDA::cudart)
        add_test(NAME cuda_burn_thermal_math COMMAND arch_cuda_burn_thermal_math)
        set_tests_properties(cuda_burn_thermal_math PROPERTIES SKIP_RETURN_CODE 77)
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
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;-Xcompiler=-march=native>)
        set_target_properties(arch_cuda_hydro_block PROPERTIES
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON)
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
        target_compile_features(arch_cuda_diffusion_rkl_parity PRIVATE cxx_std_20)
        target_include_directories(arch_cuda_diffusion_rkl_parity PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_options(arch_cuda_diffusion_rkl_parity PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;-Xcompiler=-march=native>)
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
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
        set_target_properties(arch_cuda_policy_resolution PROPERTIES
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON)
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
        # ARCH's directory-wide contract applies to our targets, not upstream
        # dependencies. Inheriting it into KLU creates the cycle
        # arch_build_contract -> KLU -> arch_build_contract and breaks exports.
        get_property(arch_saved_directory_links DIRECTORY PROPERTY LINK_LIBRARIES)
        set_property(DIRECTORY PROPERTY LINK_LIBRARIES "")
        FetchContent_MakeAvailable(suitesparse)
        set_property(DIRECTORY PROPERTY LINK_LIBRARIES "${arch_saved_directory_links}")
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
        tests/host/test_checkpoint_compatibility.cpp
        src/core/FileFingerprint.cpp
        src/io/chk/CheckpointCompatibility.cpp
        src/io/chk/ChkIO.cpp
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
        tests/host/TabularEOSRegression.cpp
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
        add_executable(sparse_klu_regression tests/host/SparseKLURegression.cpp)
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
