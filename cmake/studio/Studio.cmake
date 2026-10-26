# Source-checkout desktop entry. Scientific builds stay independent of npm.
option(ARCH_BUILD_STUDIO "Build the desktop alongside the scientific executable" OFF)
if(NOT ARCH_BUILD_STUDIO)
    return()
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT WIN32)
    message(FATAL_ERROR "ARCH_BUILD_STUDIO supports Linux/WSL and native Windows")
endif()
find_program(ARCH_STUDIO_NODE NAMES node node.exe REQUIRED)
get_filename_component(ARCH_STUDIO_NODE_DIRECTORY "${ARCH_STUDIO_NODE}" DIRECTORY)
if(WIN32)
    find_file(ARCH_STUDIO_NPM_CLI NAMES npm-cli.js
        HINTS "${ARCH_STUDIO_NODE_DIRECTORY}/node_modules/npm/bin" REQUIRED)
    set(studio_npm_command "${ARCH_STUDIO_NODE}" "${ARCH_STUDIO_NPM_CLI}")
    set(studio_path "${ARCH_STUDIO_NODE_DIRECTORY};$ENV{PATH}")
    set(studio_platform "win32")
else()
    find_program(ARCH_STUDIO_NPM NAMES npm HINTS "${ARCH_STUDIO_NODE_DIRECTORY}" REQUIRED)
    set(studio_npm_command "${ARCH_STUDIO_NPM}")
    set(studio_path "${ARCH_STUDIO_NODE_DIRECTORY}:$ENV{PATH}")
    set(studio_platform "linux")
endif()
execute_process(COMMAND "${ARCH_STUDIO_NODE}" --print "process.versions.node.split('.')[0]"
    OUTPUT_VARIABLE studio_node_major OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE studio_node_result)
if(NOT studio_node_result EQUAL 0 OR NOT studio_node_major MATCHES "^[0-9]+$"
        OR studio_node_major LESS 24)
    message(FATAL_ERROR "Studio requires native Node 24+. Set ARCH_STUDIO_NODE to its absolute executable.")
endif()
execute_process(COMMAND "${ARCH_STUDIO_NODE}" --print "process.platform"
    OUTPUT_VARIABLE studio_node_platform OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE studio_node_platform_result)
if(NOT studio_node_platform_result EQUAL 0 OR NOT studio_node_platform STREQUAL studio_platform)
    message(FATAL_ERROR "Studio Node must run on ${studio_platform}; a bridge or cross-platform runtime is not native.")
endif()

set(studio_source "${CMAKE_SOURCE_DIR}/studio")
file(GLOB_RECURSE studio_inputs CONFIGURE_DEPENDS
    "${studio_source}/src/*" "${studio_source}/host/*" "${studio_source}/desktop/*")
list(APPEND studio_inputs "${studio_source}/package.json" "${studio_source}/package-lock.json"
    "${studio_source}/index.html" "${studio_source}/tsconfig.json" "${studio_source}/vite.config.ts")
set(studio_stamp "${CMAKE_BINARY_DIR}/studio-assets.stamp")
add_custom_command(OUTPUT "${studio_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E env --unset=ELECTRON_SKIP_BINARY_DOWNLOAD
        "PATH=${studio_path}" ${studio_npm_command} ci --ignore-scripts --no-audit --no-fund
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${studio_path}"
        ${studio_npm_command} run build
    COMMAND "${CMAKE_COMMAND}" -E touch "${studio_stamp}"
    DEPENDS ${studio_inputs}
    WORKING_DIRECTORY "${studio_source}"
    COMMENT "Build Studio assets from the locked dependency list"
    VERBATIM)
add_custom_target(arch-studio-assets DEPENDS "${studio_stamp}")
add_custom_target(arch-studio-runtime
    COMMAND "${CMAKE_COMMAND}" "-DSTUDIO_NODE=${ARCH_STUDIO_NODE}"
        "-DSTUDIO_ROOT=${studio_source}" -P "${CMAKE_SOURCE_DIR}/cmake/studio/EnsureElectron.cmake"
    COMMENT "Prepare the locked native Electron desktop runtime"
    VERBATIM)
add_dependencies(arch-studio-runtime arch-studio-assets)
add_custom_target(arch-studio ALL)
add_dependencies(arch-studio arch-studio-runtime)
add_dependencies(arch-studio ARCH)
add_custom_target(arch-studio-package
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${studio_path}"
        "${ARCH_STUDIO_NODE}" "${studio_source}/desktop/package.mjs"
    WORKING_DIRECTORY "${studio_source}"
    COMMENT "Package the native Electron window, Host Node and read-only HDF5 worker"
    VERBATIM)
add_dependencies(arch-studio-package arch-studio)
if(NOT WIN32 AND NOT EXISTS "/usr/bin/xterm")
    message(WARNING "Studio Run/Restart requires /usr/bin/xterm; editing and Preview can run without it.")
endif()

file(MAKE_DIRECTORY "${ARCH_RUNTIME_OUTPUT_DIRECTORY}")
if(WIN32)
    # Batch files need literal percent signs doubled; delayed expansion is off.
    string(REPLACE "%" "%%" ARCH_STUDIO_NODE_QUOTED "${ARCH_STUDIO_NODE}")
    string(REPLACE "%" "%%" ARCH_STUDIO_ENTRY_QUOTED "${studio_source}/desktop/arch-studio.cmd")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/studio/arch-studio.cmd.in"
        "${ARCH_RUNTIME_OUTPUT_DIRECTORY}/arch-studio.cmd" @ONLY NEWLINE_STYLE CRLF)
    message(STATUS "[STUDIO] Windows entry: ${ARCH_RUNTIME_OUTPUT_DIRECTORY}/arch-studio.cmd")
else()
    string(REPLACE "'" "'\"'\"'" ARCH_STUDIO_NODE_QUOTED "${ARCH_STUDIO_NODE}")
    string(REPLACE "'" "'\"'\"'" ARCH_STUDIO_ENTRY_QUOTED "${studio_source}/desktop/arch-studio")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/studio/arch-studio.in"
        "${ARCH_RUNTIME_OUTPUT_DIRECTORY}/arch-studio" @ONLY)
    file(CHMOD "${ARCH_RUNTIME_OUTPUT_DIRECTORY}/arch-studio"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    message(STATUS "[STUDIO] Linux entry: ${ARCH_RUNTIME_OUTPUT_DIRECTORY}/arch-studio")
endif()
