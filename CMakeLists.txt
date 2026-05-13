cmake_minimum_required(VERSION 3.15)

# ==============================================================================
# Project Configuration
# ==============================================================================
project(ARCH LANGUAGES CXX)

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

# ==============================================================================
# Target Definition & Properties
# ==============================================================================
add_executable(ARCH ${SOURCES} ${SIMULATION_CASES})

# 设置 C++17 标准
target_compile_features(ARCH PRIVATE cxx_std_17)

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
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(ARCH PRIVATE OpenMP::OpenMP_CXX)
        message(STATUS "OpenMP enabled for target ARCH")
    else()
        message(WARNING "ARCH_ENABLE_OPENMP=ON but OpenMP was not found. Building without OpenMP.")
    endif()
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