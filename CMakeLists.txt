cmake_minimum_required(VERSION 3.16)

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

# 1. 打印详细编译细节
set(CMAKE_VERBOSE_MAKEFILE ON)

# 2. 导出 compile_commands.json 
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# Source File Collection
# ==============================================================================
# 使用 CONFIGURE_DEPENDS 确保新增文件时 CMake 自动刷新
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE SIMULATION_CASES CONFIGURE_DEPENDS "simulation/*.cpp")

# 排除 network 子目录下的 main.cpp 文件
list(FILTER SOURCES EXCLUDE REGEX ".*/network/.*/main\\.cpp$")

# ==============================================================================
# Target Definition & Properties
# ==============================================================================
add_executable(ARCH ${SOURCES} ${SIMULATION_CASES})

target_precompile_headers(ARCH PRIVATE
    <omp.h>
    <vector>
    <string>
    <iostream>
    <memory>
    <cmath>
    <omp.h>
    <highfive/H5File.hpp> 
    <highfive/H5DataSet.hpp>
    <highfive/H5DataSpace.hpp>
)

# 设置头文件包含路径
target_include_directories(ARCH PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/cases
)

# 设置可执行文件输出到 bin 目录
set_target_properties(ARCH PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ==============================================================================
# Dependencies: OpenMP
# ==============================================================================
if(ARCH_ENABLE_OPENMP)
    # A requested OpenMP build must never silently fall back to a serial binary.
    find_package(OpenMP REQUIRED COMPONENTS CXX)
    target_link_libraries(ARCH PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(ARCH PRIVATE ARCH_OPENMP_ENABLED=1)
    message(STATUS "OpenMP enabled for target ARCH")
else()
    target_compile_definitions(ARCH PRIVATE ARCH_OPENMP_ENABLED=0)
    message(STATUS "OpenMP disabled for target ARCH")
endif()

# ==============================================================================
# Dependencies: HDF5 (C++ Bindings)
# ==============================================================================
find_package(HDF5 COMPONENTS CXX HL REQUIRED)

if(HDF5_FOUND)
    target_include_directories(ARCH PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(ARCH PRIVATE ${HDF5_LIBRARIES} ${HDF5_CXX_LIBRARIES} ${HDF5_HL_LIBRARIES})
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
message(STATUS "HighFive headers configured via FetchContent")

# ==============================================================================
# Runtime Environment Setup & Final Status
# ==============================================================================
# 确保 output 目录存在
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output)

#  打印成功信息和路径
message(STATUS "===================================================================")
message(STATUS "[SUCCESS] ARCH Framework build environment configured successfully!")
message(STATUS "[SUCCESS] Executable : ${CMAKE_CURRENT_SOURCE_DIR}/bin/ARCH")
message(STATUS "[SUCCESS] Data Output: ${CMAKE_CURRENT_SOURCE_DIR}/output/")
message(STATUS "===================================================================")
