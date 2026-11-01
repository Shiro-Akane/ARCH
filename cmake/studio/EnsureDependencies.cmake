# node_modules belongs to the source checkout, even when several build trees use it.
file(MAKE_DIRECTORY "${STUDIO_ROOT}/.local")
file(LOCK "${STUDIO_ROOT}/.local/npm-install.lock" GUARD PROCESS TIMEOUT 120)
file(SHA256 "${STUDIO_ROOT}/package.json" manifest_sha)
file(SHA256 "${STUDIO_ROOT}/package-lock.json" lock_sha)
file(SHA256 "${STUDIO_NODE}" node_sha)
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" installer_sha)
string(SHA256 identity "${manifest_sha};${lock_sha};${node_sha};${installer_sha};${CMAKE_HOST_SYSTEM_NAME}")
set(stamp "${STUDIO_ROOT}/node_modules/.arch-dependencies")
if(EXISTS "${stamp}")
    file(READ "${stamp}" installed_identity)
    if(installed_identity STREQUAL identity AND
            EXISTS "${STUDIO_ROOT}/node_modules/.package-lock.json" AND
            EXISTS "${STUDIO_ROOT}/node_modules/typescript/bin/tsc" AND
            EXISTS "${STUDIO_ROOT}/node_modules/vite/package.json" AND
            EXISTS "${STUDIO_ROOT}/node_modules/electron/package.json")
        return()
    endif()
endif()
if(STUDIO_NPM_CLI)
    set(npm_command "${STUDIO_NODE}" "${STUDIO_NPM_CLI}")
else()
    set(npm_command "${STUDIO_NPM}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=ELECTRON_SKIP_BINARY_DOWNLOAD
    ${npm_command} ci --ignore-scripts --no-audit --no-fund
    WORKING_DIRECTORY "${STUDIO_ROOT}" RESULT_VARIABLE npm_result)
if(NOT npm_result EQUAL 0)
    message(FATAL_ERROR "Studio dependency installation failed")
endif()
file(WRITE "${stamp}" "${identity}")
