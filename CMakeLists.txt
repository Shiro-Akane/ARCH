cmake_minimum_required(VERSION 3.22)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

project(ARCH LANGUAGES C CXX)

# Build order is explicit: shared contract, backend, providers, then I/O tests.
# Modules are included in this directory (not add_subdirectory), preserving
# source properties, output paths and the directory-wide numerical contract.
# See cmake/README.md for module ownership and extension points.
include(cmake/project/BuildOptions.cmake)
include(cmake/project/Application.cmake)

include(CTest)
if(ARCH_REQUIRE_CUDSS AND NOT BUILD_TESTING)
    message(FATAL_ERROR "ARCH_REQUIRE_CUDSS requires BUILD_TESTING=ON. Build and run the arch-check-cudss target on a supported GPU.")
endif()
if(BUILD_TESTING)
    include(cmake/tests/HostTests.cmake)
endif()

if(ARCH_ENABLE_CUDA)
    include(cmake/cuda/CudaBackend.cmake)
    if(BUILD_TESTING)
        include(cmake/tests/CudaTests.cmake)
    endif()
endif()

include(cmake/dependencies/Dependencies.cmake)
if(WIN32)
    include(cmake/project/WindowsRuntime.cmake)
endif()
include(cmake/studio/Studio.cmake)
include(cmake/project/BuildIdentity.cmake)
if(BUILD_TESTING)
    arch_register_io_regression_tests()
endif()

# Runtime output directories are not part of the source-delivery archive.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output)
include(cmake/project/CapabilitySummary.cmake)
