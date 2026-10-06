# Embed one immutable, explicitly scoped provenance record in a narrow object.
# This owner runs after backend/providers so configured feature definitions exist.
# Runtime Git queries and a self-embedded executable digest are deliberately absent.

set(arch_identity_inputs ${ARCH_APPLICATION_SOURCES} ${ARCH_DISPATCH_SOURCES}
    ${ARCH_GRAVITY_CPU_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
# Bind extracted mathematical leaves and actual CUDA object owners as well as
# APP collection variables; extraction must not silently drop a source identity.
set(arch_identity_targets ARCH arch_build_contract arch_solver_dispatch arch_gravity_cpu arch_diffusion_math)
get_property(arch_identity_directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS arch_identity_directory_targets)
    if(target MATCHES "^arch_cuda_")
        list(APPEND arch_identity_targets "${target}")
    endif()
endforeach()
list(REMOVE_DUPLICATES arch_identity_targets)
foreach(target IN LISTS arch_identity_targets)
    get_target_property(target_sources "${target}" SOURCES)
    if(target_sources)
        foreach(source IN LISTS target_sources)
            # TARGET_OBJECTS references name object targets whose actual sources
            # are already collected above; they are not filesystem paths.
            if(NOT source MATCHES "\\$<")
                list(APPEND arch_identity_inputs "${source}")
            endif()
        endforeach()
    endif()
endforeach()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/math/io/PlotIdentityFixture.h")
    list(APPEND arch_identity_inputs "${CMAKE_CURRENT_SOURCE_DIR}/tests/math/io/PlotIdentityFixture.h")
endif()
# Translation units come only from their configured target SOURCES owners.
# Directory enumeration is restricted after enumeration to headers/build
# metadata: it fingerprints included mathematics without registering another
# CUDA compilation source list or accepting an unowned .cu translation unit.
foreach(root src include simulation cmake)
    file(GLOB_RECURSE arch_identity_headers CONFIGURE_DEPENDS
        LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/${root}/*")
    list(FILTER arch_identity_headers INCLUDE REGEX "\\.(h|hpp|inc|cuh|inl|tpp|ipp|cmake)$")
    list(APPEND arch_identity_inputs ${arch_identity_headers})
endforeach()
foreach(root "${ARCH_CUSTOM_NETWORK_ROOT}" "${ARCH_CUSTOM_REGISTRY_DIR}")
    if(IS_DIRECTORY "${root}")
        file(GLOB_RECURSE arch_identity_custom CONFIGURE_DEPENDS
            LIST_DIRECTORIES false "${root}/*")
        list(FILTER arch_identity_custom INCLUDE REGEX "\\.(h|hpp|inc|cuh|inl|tpp|ipp|cmake)$")
        list(APPEND arch_identity_inputs ${arch_identity_custom})
    endif()
endforeach()
list(TRANSFORM arch_identity_inputs PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/"
    REGEX "^[^/].*")
list(REMOVE_DUPLICATES arch_identity_inputs)
list(SORT arch_identity_inputs)
set(arch_identity_source "arch-project-source-manifest-1\n")
foreach(path IN LISTS arch_identity_inputs)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Build provenance input is absent: ${path}")
    endif()
    if(path MATCHES "[\n\r\t]")
        message(FATAL_ERROR "Build provenance paths cannot contain control separators")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(SHA256 "${path}" digest)
    file(RELATIVE_PATH label "${CMAKE_CURRENT_SOURCE_DIR}" "${path}")
    string(APPEND arch_identity_source "${label}\t${digest}\n")
endforeach()

set(arch_identity_profile "arch-configured-compiler-profile-1\n")
# CMake/profile fingerprints describe configured project compilation; they do
# not claim exhaustive runtime DSOs, operating-system or driver attestation.
foreach(name CMAKE_VERSION CMAKE_GENERATOR CMAKE_BUILD_TYPE CMAKE_SYSTEM_NAME
    CMAKE_SYSTEM_PROCESSOR CMAKE_CXX_COMPILER_LAUNCHER CMAKE_CUDA_COMPILER_LAUNCHER CMAKE_C_COMPILER CMAKE_C_COMPILER_ID CMAKE_C_COMPILER_VERSION
    CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_ID CMAKE_CXX_COMPILER_VERSION
    CMAKE_CUDA_COMPILER CMAKE_CUDA_COMPILER_ID CMAKE_CUDA_COMPILER_VERSION
    CMAKE_CUDA_ARCHITECTURES CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE
    CMAKE_C_FLAGS CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CUDA_FLAGS CMAKE_CUDA_FLAGS_RELEASE CMAKE_EXE_LINKER_FLAGS
    CMAKE_EXE_LINKER_FLAGS_RELEASE CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG
    CMAKE_CUDA_FLAGS_DEBUG CMAKE_EXE_LINKER_FLAGS_DEBUG
    CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CUDA_FLAGS_RELWITHDEBINFO
    CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_MINSIZEREL
    CMAKE_CUDA_FLAGS_MINSIZEREL CMAKE_EXE_LINKER_FLAGS_MINSIZEREL)
    string(LENGTH "${${name}}" value_length)
    string(APPEND arch_identity_profile "${name}=${value_length}:${${name}}\n")
endforeach()
foreach(name CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_CUDA_COMPILER)
    if(EXISTS "${${name}}")
        file(SHA256 "${${name}}" driver_sha256)
        string(APPEND arch_identity_profile "${name}.driver_sha256=${driver_sha256}\n")
    endif()
endforeach()
get_cmake_property(arch_identity_cache CACHE_VARIABLES)
list(SORT arch_identity_cache)
foreach(name IN LISTS arch_identity_cache)
    if(name MATCHES "^ARCH_" AND NOT name MATCHES "^ARCH_IDENTITY_")
        string(LENGTH "${${name}}" value_length)
        string(APPEND arch_identity_profile "${name}=${value_length}:${${name}}\n")
    endif()
endforeach()
foreach(target IN LISTS arch_identity_targets)
    foreach(property COMPILE_DEFINITIONS COMPILE_OPTIONS COMPILE_FEATURES INCLUDE_DIRECTORIES LINK_OPTIONS
        LINK_LIBRARIES INTERFACE_COMPILE_DEFINITIONS INTERFACE_COMPILE_OPTIONS INTERFACE_COMPILE_FEATURES
        INTERFACE_INCLUDE_DIRECTORIES INTERFACE_LINK_OPTIONS INTERPROCEDURAL_OPTIMIZATION_RELEASE
        INTERPROCEDURAL_OPTIMIZATION_DEBUG INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL)
        get_target_property(value "${target}" "${property}")
        if(value MATCHES "-NOTFOUND$")
            set(value "")
        endif()
        string(LENGTH "${value}" value_length)
        string(APPEND arch_identity_profile "${target}.${property}=${value_length}:${value}\n")
    endforeach()
endforeach()
set(arch_identity_head "")
set(arch_identity_dirty "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse HEAD
        RESULT_VARIABLE result OUTPUT_VARIABLE arch_identity_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT result EQUAL 0)
        set(arch_identity_head "")
    else()
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" status --porcelain --untracked-files=no
            RESULT_VARIABLE status_result OUTPUT_VARIABLE status ERROR_QUIET)
        if(status_result EQUAL 0)
            if(status STREQUAL "")
                set(arch_identity_dirty "false")
            else()
                set(arch_identity_dirty "true")
            endif()
        endif()
    endif()
endif()
# Delimiters are derived from content and checked, so quoted paths cannot escape
# the generated C++ raw-string literals.
set(arch_identity_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/identity")
file(MAKE_DIRECTORY "${arch_identity_generated}")
set(header "#pragma once\nnamespace arch_generated_identity {\n")
foreach(pair source_manifest profile_record source_git_head source_git_dirty)
    if(pair STREQUAL "source_manifest")
        set(value "${arch_identity_source}")
    elseif(pair STREQUAL "profile_record")
        set(value "${arch_identity_profile}")
    elseif(pair STREQUAL "source_git_head")
        set(value "${arch_identity_head}")
    else()
        set(value "${arch_identity_dirty}")
    endif()
    string(SHA256 value_sha "${value}")
    string(SUBSTRING "${value_sha}" 0 10 suffix)
    set(delimiter "ARCHID${suffix}")
    string(FIND "${value}" ")${delimiter}\"" collision)
    if(NOT collision EQUAL -1)
        message(FATAL_ERROR "Generated build identity delimiter collision")
    endif()
    string(APPEND header "inline constexpr const char* ${pair} = R\"${delimiter}(${value})${delimiter}\";\n")
endforeach()
string(APPEND header "}\n")
set(generated_header "${arch_identity_generated}/BuildIdentity.generated.h")
# configure_file COPYONLY changes the output timestamp only when bytes change.
file(WRITE "${generated_header}.candidate" "${header}")
configure_file("${generated_header}.candidate" "${generated_header}" COPYONLY)
file(REMOVE "${generated_header}.candidate")
set_source_files_properties(src/core/files/BuildIdentity.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${arch_identity_generated}"
    COMPILE_DEFINITIONS "ARCH_IDENTITY_BUILD_CONFIG=\"$<CONFIG>\"")
