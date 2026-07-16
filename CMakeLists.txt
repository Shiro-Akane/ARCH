cmake_minimum_required(VERSION 3.18)

# ==============================================================================
# Project Configuration
# ==============================================================================
project(ARCH LANGUAGES CXX)
# 启用 ccache 进行编译加速
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ARCH_ENABLE_OPENMP "Enable OpenMP parallelization" ON)
option(ARCH_ENABLE_CUDA "Build the optional CUDA runtime backend" OFF)
option(ARCH_VERBOSE_BUILD "Print every compiler and linker command" OFF)
set(ARCH_CUDA_ARCHITECTURES "90" CACHE STRING
    "CUDA architectures compiled into ARCH (for example: 90 or 80;86;90)")
set(ARCH_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" CACHE PATH
    "Directory for ARCH executables from this build tree")

if(ARCH_ENABLE_CUDA)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 20)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    if(CCACHE_PROGRAM)
        set(CMAKE_CUDA_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    endif()
endif()

# Detailed command echo is opt-in.  Keeping it off avoids avoidable terminal
# and log I/O during the already expensive template-instantiation build.
set(CMAKE_VERBOSE_MAKEFILE ${ARCH_VERBOSE_BUILD})

# 2. 导出 compile_commands.json 
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# Source File Collection
# ==============================================================================
# Explicit manifests keep an unrelated new source file from regenerating the
# entire target and its PCH.  SolverDispatch is isolated in its own object
# target because it currently owns the large runtime-selectable template
# matrix.  P2 will split that matrix further into explicit hydro/burn objects.
set(ARCH_APPLICATION_SOURCES
    src/main.cpp
    src/core/ProblemHelper.cpp
    src/physics/network/aprox13/NetAprox13.cpp
    src/physics/network/aprox19/NetAprox19.cpp
    src/physics/network/aprox21/NetAprox21.cpp
    src/physics/network/iso7/NetIso7.cpp
    src/runtime/ComputeBackend.cpp
    src/runtime/RuntimeDispatchRegistry.cpp
    simulation/Cellular/Cellular.cpp
    simulation/RTinstability/RT_instab.cpp
    simulation/Sod/Sod.cpp
)

set(ARCH_CUDA_SOURCES)
if(ARCH_ENABLE_CUDA)
    list(APPEND ARCH_CUDA_SOURCES src/cuda/CudaRuntime.cu)
else()
    list(APPEND ARCH_APPLICATION_SOURCES src/cuda/CudaRuntime.cpp)
endif()

# ==============================================================================
# Target Definition & Properties
# ==============================================================================
add_library(arch_solver_dispatch OBJECT
    src/driver/SolverDispatch.cpp)

add_executable(ARCH
    ${ARCH_APPLICATION_SOURCES}
    ${ARCH_CUDA_SOURCES}
    $<TARGET_OBJECTS:arch_solver_dispatch>)

add_executable(ARCH_runtime_dispatch_registry_test EXCLUDE_FROM_ALL
    tests/runtime_dispatch_registry_test.cpp
    src/runtime/RuntimeDispatchRegistry.cpp
    src/runtime/ComputeBackend.cpp)
target_compile_features(ARCH_runtime_dispatch_registry_test PRIVATE cxx_std_20)
target_include_directories(ARCH_runtime_dispatch_registry_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(ARCH_runtime_dispatch_registry_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${ARCH_RUNTIME_OUTPUT_DIRECTORY}")

foreach(target ARCH arch_solver_dispatch)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/cases)
endforeach()

if(ARCH_ENABLE_CUDA)
    # The project PCH contains host-only HDF5/OpenMP headers.  Keep it out of
    # nvcc translation units; CUDA kernels include only their explicit API.
    set_source_files_properties(${ARCH_CUDA_SOURCES} PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON)
endif()

# Only the one heavy translation unit owns this PCH.  Lightweight application,
# registry, problem, and CUDA-runtime changes no longer invalidate it.
target_precompile_headers(arch_solver_dispatch PRIVATE
    <omp.h>
    <vector>
    <string>
    <iostream>
    <memory>
    <cmath>
    <highfive/H5File.hpp> 
    <highfive/H5DataSet.hpp>
    <highfive/H5DataSpace.hpp>
)

# 设置可执行文件输出到 bin 目录
set_target_properties(ARCH PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY "${ARCH_RUNTIME_OUTPUT_DIRECTORY}"
)

# ==============================================================================
# Dependencies: OpenMP
# ==============================================================================
if(ARCH_ENABLE_OPENMP)
    # A requested OpenMP build must never silently fall back to a serial binary.
    find_package(OpenMP REQUIRED COMPONENTS CXX)
    target_link_libraries(ARCH PRIVATE OpenMP::OpenMP_CXX)
    target_link_libraries(arch_solver_dispatch PRIVATE OpenMP::OpenMP_CXX)
    foreach(target ARCH arch_solver_dispatch)
        target_compile_definitions(${target} PRIVATE ARCH_OPENMP_ENABLED=1)
    endforeach()
    message(STATUS "OpenMP enabled for target ARCH")
else()
    foreach(target ARCH arch_solver_dispatch)
        target_compile_definitions(${target} PRIVATE ARCH_OPENMP_ENABLED=0)
    endforeach()
    message(STATUS "OpenMP disabled for target ARCH")
endif()

# ==============================================================================
# Dependencies: CUDA (optional fat binary backend)
# ==============================================================================
if(ARCH_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
    set_target_properties(ARCH PROPERTIES
        CUDA_ARCHITECTURES "${ARCH_CUDA_ARCHITECTURES}"
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON
        CUDA_RUNTIME_LIBRARY Static
    )
    foreach(target ARCH arch_solver_dispatch)
        target_compile_definitions(${target} PRIVATE ARCH_CUDA_ENABLED=1)
    endforeach()
    message(STATUS "CUDA runtime backend enabled for architectures: ${ARCH_CUDA_ARCHITECTURES}")

    # Strict current-network device validations are deliberately separate from
    # ARCH and absent from CPU-only builds.
    function(arch_add_cuda_validation target)
        # Validation and benchmark programs are opt-in targets.  Keeping them
        # out of the default ALL graph prevents routine ARCH builds from
        # compiling several additional copies of the heavy network headers.
        add_executable(${target} EXCLUDE_FROM_ALL ${ARGN})
        target_include_directories(${target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_compile_definitions(${target} PRIVATE ARCH_CUDA_ENABLED=1)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--fmad=false;--prec-div=true;--prec-sqrt=true;--expt-relaxed-constexpr;-Xcompiler=-ffp-contract=off,-fno-fast-math>)
        set_target_properties(${target} PROPERTIES
            CUDA_ARCHITECTURES "${ARCH_CUDA_ARCHITECTURES}"
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON
            CUDA_RUNTIME_LIBRARY Static
            RUNTIME_OUTPUT_DIRECTORY "${ARCH_RUNTIME_OUTPUT_DIRECTORY}")
    endfunction()

    arch_add_cuda_validation(ARCH_cuda_aprox13_validation
        tools/cuda_aprox13_validation.cu)
    arch_add_cuda_validation(ARCH_cuda_iso7_validation
        tools/cuda_iso7_validation.cu)
    arch_add_cuda_validation(ARCH_cuda_aprox13_be_validation
        tools/cuda_aprox13_be_validation.cu)
    arch_add_cuda_validation(ARCH_cuda_aprox13_be_warp_validation
        tools/cuda_aprox13_be_warp_validation.cu)
    arch_add_cuda_validation(ARCH_cuda_aprox13_batch_api_validation
        tools/cuda_aprox13_batch_api_validation.cu
        src/cuda/Aprox13FixedCvValidationBatch.cu)
    arch_add_cuda_validation(ARCH_cuda_aprox19_validation
        tools/cuda_extended_network_validation.cu)
    target_compile_definitions(ARCH_cuda_aprox19_validation PRIVATE
        TIMMES_VALIDATE_APROX19=1)
    arch_add_cuda_validation(ARCH_cuda_aprox21_validation
        tools/cuda_extended_network_validation.cu)
    target_compile_definitions(ARCH_cuda_aprox21_validation PRIVATE
        TIMMES_VALIDATE_APROX21=1)
    arch_add_cuda_validation(ARCH_cuda_aprox19_resource
        tools/cuda_extended_network_resource.cu)
    target_compile_definitions(ARCH_cuda_aprox19_resource PRIVATE
        TIMMES_VALIDATE_APROX19=1)
    arch_add_cuda_validation(ARCH_cuda_aprox21_resource
        tools/cuda_extended_network_resource.cu)
    target_compile_definitions(ARCH_cuda_aprox21_resource PRIVATE
        TIMMES_VALIDATE_APROX21=1)

    # Independent current-network microbenchmark.  It compares the same
    # translated Timmes aprox13 RHS, complete analytic Jacobian/temperature
    # terms and implicit LHS on CPU/OpenMP and CUDA.  This target intentionally
    # does not claim an end-to-end hydro/ODE/NSE speedup.
    if(ARCH_ENABLE_OPENMP)
        add_executable(ARCH_cuda_aprox13_benchmark EXCLUDE_FROM_ALL
            tools/cuda_aprox13_benchmark.cu)
        target_include_directories(ARCH_cuda_aprox13_benchmark PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_compile_definitions(ARCH_cuda_aprox13_benchmark PRIVATE
            ARCH_CUDA_ENABLED=1 ARCH_OPENMP_ENABLED=1)
        target_compile_options(ARCH_cuda_aprox13_benchmark PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr;-Xcompiler=-fopenmp>)
        target_link_libraries(ARCH_cuda_aprox13_benchmark PRIVATE
            OpenMP::OpenMP_CXX)
        set_target_properties(ARCH_cuda_aprox13_benchmark PROPERTIES
            CUDA_ARCHITECTURES "${ARCH_CUDA_ARCHITECTURES}"
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON
            CUDA_RUNTIME_LIBRARY Static
            RUNTIME_OUTPUT_DIRECTORY "${ARCH_RUNTIME_OUTPUT_DIRECTORY}"
        )
    endif()
else()
    foreach(target ARCH arch_solver_dispatch)
        target_compile_definitions(${target} PRIVATE ARCH_CUDA_ENABLED=0)
    endforeach()
    message(STATUS "CUDA runtime backend disabled (pure C++ build)")
endif()

# ==============================================================================
# Dependencies: HDF5 (C++ Bindings)
# ==============================================================================
find_package(HDF5 COMPONENTS CXX HL REQUIRED)

if(HDF5_FOUND)
    target_include_directories(ARCH PRIVATE ${HDF5_INCLUDE_DIRS})
    target_include_directories(arch_solver_dispatch PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(ARCH PRIVATE ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
    target_link_libraries(arch_solver_dispatch PRIVATE ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
    message(STATUS "HDF5 C++ libraries linked successfully")
endif()

include(FetchContent)
FetchContent_Declare(
    highfive
    GIT_REPOSITORY https://github.com/BlueBrain/HighFive.git
    GIT_TAG v2.9.0
)
#  拉取 HighFive
FetchContent_Populate(highfive)

# 将 HighFive 的 include 目录添加到 ARCH 的包含路径中
target_include_directories(ARCH PRIVATE ${highfive_SOURCE_DIR}/include)
target_include_directories(arch_solver_dispatch PRIVATE ${highfive_SOURCE_DIR}/include)
message(STATUS "HighFive headers configured via FetchContent")

# ==============================================================================
# Runtime Environment Setup & Final Status
# ==============================================================================
# 确保 output 目录存在
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output)

#  打印成功信息和路径
message(STATUS "===================================================================")
message(STATUS "[SUCCESS] ARCH Framework build environment configured successfully!")
message(STATUS "[SUCCESS] Executable : ${ARCH_RUNTIME_OUTPUT_DIRECTORY}/ARCH")
message(STATUS "[SUCCESS] Data Output: ${CMAKE_CURRENT_SOURCE_DIR}/output/")
message(STATUS "===================================================================")
