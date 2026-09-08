# Link optional CPU providers and required I/O dependencies to existing targets.
# This runs after test/backend declaration so their narrow dependencies stay local.

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
