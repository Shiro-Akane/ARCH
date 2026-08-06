cmake_minimum_required(VERSION 3.18)

# ==============================================================================
# Project Configuration
# ==============================================================================
project(ARCH LANGUAGES CXX)

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
endif()

option(ARCH_ENABLE_OPENMP "Enable OpenMP parallelization" ON)
option(ARCH_VERBOSE_BUILD "Print every compiler and linker command" OFF)

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
    "simulation"
)

set(ARCH_APPLICATION_SOURCES src/main.cpp)
foreach(dir IN LISTS APP_SRC_DIRS)
    file(GLOB_RECURSE dir_srcs "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp")
    list(APPEND ARCH_APPLICATION_SOURCES ${dir_srcs})
endforeach()

# Dispatch sources (isolated in its own object target to manage template instantiation)
set(DISPATCH_SRC_DIRS
    "src/driver/dispatch"
)
set(ARCH_DISPATCH_SOURCES src/driver/SolverDispatch.cpp)
foreach(dir IN LISTS DISPATCH_SRC_DIRS)
    file(GLOB dir_srcs "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp")
    list(APPEND ARCH_DISPATCH_SOURCES ${dir_srcs})
endforeach()

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
    )
endforeach()

# Precompile headers for the heavy template translation unit
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

set_target_properties(ARCH PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${ARCH_RUNTIME_OUTPUT_DIRECTORY}")

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
    message(STATUS "[DEP] OpenMP enabled for target ARCH")
else()
    foreach(target ARCH arch_solver_dispatch)
        target_compile_definitions(${target} PRIVATE ARCH_OPENMP_ENABLED=0)
    endforeach()
    message(STATUS "[DEP] OpenMP disabled for target ARCH")
endif()

# 2. HDF5
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

# ==============================================================================
# Runtime Environment Setup & Final Status
# ==============================================================================
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output)

message(STATUS "===================================================================")
message(STATUS "[SUCCESS] ARCH Framework CPU build environment configured successfully!")
message(STATUS "[SUCCESS] Executable : ${ARCH_RUNTIME_OUTPUT_DIRECTORY}/ARCH")
message(STATUS "[SUCCESS] Data Output: ${CMAKE_CURRENT_SOURCE_DIR}/output/")
message(STATUS "===================================================================")