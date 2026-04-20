cmake_minimum_required(VERSION 3.10)

# ==============================================================================
# Project Configuration
# ==============================================================================
project(ARCH)

option(ARCH_ENABLE_OPENMP "Enable OpenMP parallelization" ON)

set(CMAKE_VERBOSE_MAKEFILE ON)

# Set C++ Standard
# We enforce C++17 to support modern template features.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# 1. Source File Collection
# ==============================================================================
# The core project is primarily header-only (template-based).
# However, we use GLOB_RECURSE to catch any implementation (.cpp) files 
# in 'src' or 'simulation' to ensure they are compiled if present.
# Note: If you add new source files, you may need to re-run CMake.
file(GLOB_RECURSE SIMULATION_CASES "simulation/*.cpp")
file(GLOB_RECURSE SOURCES "src/*.cpp")

# ==============================================================================
# 2. Include Directories
# ==============================================================================
# Configure include paths to allow modular imports.
# Example: #include "core/Grid.h" resolves to ${CMAKE_SOURCE_DIR}/src/core/Grid.h
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/cases
)

# ==============================================================================
# 3. Output Directory Setup
# ==============================================================================
# Direct the compiled executable to the /bin directory for cleaner organization,
# keeping it separate from intermediate build files.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# ==============================================================================
# 4. Target Definition
# ==============================================================================
# Create the executable named 'ARCH' linking all collected sources.
add_executable(ARCH ${SOURCES} ${SIMULATION_CASES})

if(ARCH_ENABLE_OPENMP)
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(ARCH PRIVATE OpenMP::OpenMP_CXX)
        message(STATUS "OpenMP enabled for target ARCH")
    else()
        message(WARNING "ARCH_ENABLE_OPENMP=ON but OpenMP was not found. Building without OpenMP.")
    endif()
endif()

# ==============================================================================
# 5. Runtime Environment Setup
# ==============================================================================
# Automatically create an 'output' directory for simulation data/logs.
# This ensures the directory exists before the program runs.
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/output)

# Feedback message
message(STATUS "Build setup complete. Executable will be in /bin, data will go to /output")