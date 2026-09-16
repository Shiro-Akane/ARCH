# Select a memory-efficient linker only after it links the selected compilers'
# C and C++ LTO archives together. Keep the compiler default as a tested fallback.
include_guard(GLOBAL)

function(arch_probe_ipo_linker link_option result output)
    string(MD5 probe_id "${link_option}")
    set(probe_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/ArchIpoLinker/${probe_id}")
    file(MAKE_DIRECTORY "${probe_dir}/src")
    file(WRITE "${probe_dir}/src/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.22)
project(ArchIpoLinkerProbe LANGUAGES C CXX)
add_library(probe_c STATIC value.c)
add_library(probe_cxx STATIC value.cpp)
add_executable(probe main.cpp)
set_property(TARGET probe_c probe_cxx probe
    PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_link_libraries(probe PRIVATE probe_c probe_cxx)
if(ARCH_PROBE_LINK_OPTION)
    target_link_options(probe PRIVATE "${ARCH_PROBE_LINK_OPTION}")
endif()
]=])
    file(WRITE "${probe_dir}/src/value.c" "int c_value(void) { return 19; }\n")
    file(WRITE "${probe_dir}/src/value.cpp" "int cxx_value() { return 23; }\n")
    file(WRITE "${probe_dir}/src/main.cpp"
        "extern \"C\" int c_value(void);\nint cxx_value();\nint main() { return c_value() + cxx_value() != 42; }\n")

    # Check the optimized flags, not a Debug-only IPO probe. Explicit forwarding
    # also works with the project's CMake 3.22 minimum (before CMP0138).
    set(probe_config Release)
    if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(probe_config RelWithDebInfo)
    endif()
    string(TOUPPER "${probe_config}" probe_config_upper)
    set(probe_flags "-DCMAKE_BUILD_TYPE:STRING=${probe_config}"
        "-DARCH_PROBE_LINK_OPTION:STRING=${link_option}")
    foreach(variable CMAKE_C_COMPILER CMAKE_CXX_COMPILER
            CMAKE_C_COMPILER_ARG1 CMAKE_CXX_COMPILER_ARG1
            CMAKE_C_FLAGS CMAKE_CXX_FLAGS
            CMAKE_C_FLAGS_${probe_config_upper} CMAKE_CXX_FLAGS_${probe_config_upper}
            CMAKE_EXE_LINKER_FLAGS CMAKE_EXE_LINKER_FLAGS_${probe_config_upper}
            CMAKE_C_STANDARD CMAKE_CXX_STANDARD
            CMAKE_C_STANDARD_REQUIRED CMAKE_CXX_STANDARD_REQUIRED)
        if(DEFINED ${variable})
            list(APPEND probe_flags "-D${variable}:STRING=${${variable}}")
        endif()
    endforeach()
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${probe_config}")
    try_compile(probe_passed "${probe_dir}/build" "${probe_dir}/src"
        ArchIpoLinkerProbe probe
        CMAKE_FLAGS ${probe_flags}
        OUTPUT_VARIABLE probe_output)
    set(${result} "${probe_passed}" PARENT_SCOPE)
    set(${output} "${probe_output}" PARENT_SCOPE)
    unset(probe_passed CACHE)
endfunction()

function(arch_select_ipo_linker output)
    find_program(MOLD_LINKER mold)
    find_program(LLD_LINKER NAMES ld.lld lld)
    set(candidates "")
    if(MOLD_LINKER)
        list(APPEND candidates mold)
    endif()
    if(LLD_LINKER)
        list(APPEND candidates lld)
    endif()
    list(APPEND candidates default)

    foreach(candidate IN LISTS candidates)
        set(link_option "")
        if(NOT candidate STREQUAL "default")
            set(link_option "-fuse-ld=${candidate}")
        endif()
        arch_probe_ipo_linker("${link_option}" passed probe_output)
        if(passed)
            message(STATUS "[OPT] Using ${candidate} linker; combined C/C++ optimized LTO link passed")
            set(${output} "${link_option}" PARENT_SCOPE)
            return()
        endif()
        message(STATUS "[OPT] ${candidate} linker rejected by the optimized LTO link check")
        file(APPEND "${CMAKE_BINARY_DIR}/CMakeFiles/CMakeError.log"
            "ARCH ${candidate} optimized LTO link check failed:\n${probe_output}\n")
    endforeach()
    message(FATAL_ERROR
        "LTO is supported, but no candidate linker can link the selected C/C++ "
        "optimized objects. Check compiler versions and linker flags; see "
        "CMakeFiles/CMakeError.log. LTO has not been silently disabled.")
endfunction()
