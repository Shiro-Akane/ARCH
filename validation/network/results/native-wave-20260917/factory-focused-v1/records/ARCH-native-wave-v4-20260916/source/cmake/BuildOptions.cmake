# Language, optimization and user-facing build options.
# Included in the project directory before any ARCH target is declared.

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
option(ARCH_VERBOSE_BUILD "Show dependency configuration and verbose Makefile commands" OFF)
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
