if(NOT EXISTS "${STUDIO_ROOT}/node_modules/electron/package.json")
    message(FATAL_ERROR "Studio dependencies are missing; build arch-studio-assets first")
endif()
file(READ "${STUDIO_ROOT}/package.json" studio_manifest)
file(READ "${STUDIO_ROOT}/package-lock.json" studio_lock)
file(READ "${STUDIO_ROOT}/node_modules/electron/package.json" electron_manifest)
string(JSON wanted GET "${studio_manifest}" devDependencies electron)
string(JSON locked GET "${studio_lock}" packages node_modules/electron version)
string(JSON root_locked GET "${studio_lock}" packages "" devDependencies electron)
string(JSON installed GET "${electron_manifest}" version)
if(NOT wanted MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$" OR
        NOT wanted STREQUAL locked OR NOT wanted STREQUAL root_locked OR NOT wanted STREQUAL installed)
    message(FATAL_ERROR "Electron manifest, lock and installed package differ; rebuild arch-studio-assets")
endif()
if(CMAKE_HOST_WIN32)
    set(electron_binary "${STUDIO_ROOT}/node_modules/electron/dist/electron.exe")
else()
    set(electron_binary "${STUDIO_ROOT}/node_modules/electron/dist/electron")
endif()
# The official installer also verifies dist/version and path.txt. Merely finding
# an old executable must not skip preparing the newly locked runtime.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=ELECTRON_SKIP_BINARY_DOWNLOAD
    "${STUDIO_NODE}" "${STUDIO_ROOT}/node_modules/electron/install.js"
    WORKING_DIRECTORY "${STUDIO_ROOT}" RESULT_VARIABLE electron_result)
if(NOT electron_result EQUAL 0 OR NOT EXISTS "${electron_binary}")
    message(FATAL_ERROR "The locked native Electron runtime could not be prepared")
endif()
