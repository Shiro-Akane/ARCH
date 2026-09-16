cmake_minimum_required(VERSION 3.22)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

project(ARCH LANGUAGES C CXX)

# Build order is explicit: shared contract, backend, providers, then I/O tests.
# Modules are included in this directory (not add_subdirectory), preserving
# source properties, output paths and the directory-wide numerical contract.
# See cmake/README.md for module ownership and extension points.
include(cmake/BuildOptions.cmake)
include(cmake/Application.cmake)

include(CTest)
if(BUILD_TESTING)
    include(cmake/tests/HostTests.cmake)
endif()

if(ARCH_ENABLE_CUDA)
    include(cmake/CudaBackend.cmake)
    if(BUILD_TESTING)
        include(cmake/tests/CudaTests.cmake)
    endif()
endif()

include(cmake/Dependencies.cmake)
if(BUILD_TESTING)
    arch_register_io_regression_tests()
endif()

# Runtime output directories are not part of the source-delivery archive.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output)
message(STATUS "===================================================================")
message(STATUS "[SUCCESS] ARCH Framework build environment configured successfully!")
message(STATUS "[SUCCESS] Executable : ${ARCH_RUNTIME_OUTPUT_DIRECTORY}/ARCH")
message(STATUS "[SUCCESS] Data Output: ${CMAKE_CURRENT_SOURCE_DIR}/output/")
message(STATUS "===================================================================")
