# Collect application sources and establish the shared compilation contract.
# Target ownership stays in the project directory, including when tests are off.

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

include("${CMAKE_CURRENT_LIST_DIR}/CustomNetworks.cmake")

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
