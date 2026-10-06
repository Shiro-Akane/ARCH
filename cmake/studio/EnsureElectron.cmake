if(NOT EXISTS "${STUDIO_ROOT}/node_modules/electron/package.json")
    message(FATAL_ERROR "Studio dependencies are missing; build arch-studio-assets first")
endif()
set(electron_binary "${STUDIO_ROOT}/node_modules/electron/dist/electron")
if(NOT EXISTS "${electron_binary}")
    # Dependency installation may disable lifecycle scripts. Installing the
    # locked Electron runtime is an explicit desktop build component.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=ELECTRON_SKIP_BINARY_DOWNLOAD
        "${STUDIO_NODE}" "${STUDIO_ROOT}/node_modules/electron/install.js"
        WORKING_DIRECTORY "${STUDIO_ROOT}" RESULT_VARIABLE electron_result)
    if(NOT electron_result EQUAL 0 OR NOT EXISTS "${electron_binary}")
        message(FATAL_ERROR "The locked Linux Electron runtime could not be prepared")
    endif()
endif()
