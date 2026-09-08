# Validate and register generated networks for the shared CPU/CUDA registry.
# Application.cmake supplies the filtered source list and validated root paths.

# Discover generated custom networks without editing C++ dispatch source.
set(ARCH_CUSTOM_REGISTRY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(ARCH_CUSTOM_REGISTRY_HEADER
    "${ARCH_CUSTOM_REGISTRY_DIR}/CustomNetworkRegistry.generated.h")
set(ARCH_CUSTOM_TYPES_HEADER
    "${ARCH_CUSTOM_REGISTRY_DIR}/CustomNetworks.generated.h")
file(MAKE_DIRECTORY "${ARCH_CUSTOM_REGISTRY_DIR}")
set(ARCH_CUSTOM_REGISTRY_CONTENT "#pragma once\n")
set(ARCH_CUSTOM_TYPES_CONTENT
    "#pragma once\n#include \"CustomNetworkRegistry.generated.h\"\n")
set(ARCH_CUSTOM_ALL "#define ARCH_FOR_EACH_CUSTOM_NETWORK(M)")
set(ARCH_CUSTOM_CPU_ONLY
    "#define ARCH_FOR_EACH_CPU_ONLY_CUSTOM_NETWORK(M)")
set(ARCH_CUSTOM_CUDA "#define ARCH_FOR_EACH_CUDA_CUSTOM_NETWORK(M)")
set(ARCH_CUSTOM_LAYOUT "#define ARCH_FOR_EACH_CUSTOM_NETWORK_LAYOUT(M)")
set(ARCH_CUSTOM_DISCOVERED_IDS "")
set(ARCH_CUSTOM_DISCOVERY_COUNT 0)
set(ARCH_CUSTOM_COUNT 0)
set(ARCH_CUSTOM_CPU_ONLY_COUNT 0)
set(ARCH_CUSTOM_CUDA_COUNT 0)
set(ARCH_CUSTOM_CUDA_IDS "")
set(ARCH_CUSTOM_RESERVED_IDS custom none null ideal helmholtz tabular)
file(GLOB ARCH_CUSTOM_NETWORK_CONFIGS CONFIGURE_DEPENDS
    "${ARCH_CUSTOM_NETWORK_ROOT}/*/network.cmake")
list(SORT ARCH_CUSTOM_NETWORK_CONFIGS)
foreach(custom_config IN LISTS ARCH_CUSTOM_NETWORK_CONFIGS)
    unset(ARCH_CUSTOM_NETWORK_ID)
    unset(ARCH_CUSTOM_NETWORK_TYPE)
    unset(ARCH_CUSTOM_NETWORK_HEADER)
    unset(ARCH_CUSTOM_NETWORK_SOURCE)
    include("${custom_config}")
    if(NOT ARCH_CUSTOM_NETWORK_ID OR NOT ARCH_CUSTOM_NETWORK_TYPE
       OR NOT EXISTS "${ARCH_CUSTOM_NETWORK_HEADER}"
       OR NOT EXISTS "${ARCH_CUSTOM_NETWORK_SOURCE}")
        message(FATAL_ERROR "Invalid generated custom network package: ${custom_config}")
    endif()

    get_filename_component(custom_dir "${custom_config}" DIRECTORY)
    get_filename_component(custom_folder "${custom_dir}" NAME)
    set(custom_manifest "${custom_dir}/manifest.json")
    if(NOT EXISTS "${custom_manifest}")
        message(FATAL_ERROR
            "Generated custom network is missing manifest.json: ${custom_config}")
    endif()
    file(READ "${custom_manifest}" custom_manifest_json)
    string(JSON custom_manifest_schema ERROR_VARIABLE custom_schema_error
        GET "${custom_manifest_json}" schema_version)
    string(JSON custom_generator_version ERROR_VARIABLE custom_generator_error
        GET "${custom_manifest_json}" generator_version)
    string(JSON custom_manifest_id ERROR_VARIABLE custom_id_error
        GET "${custom_manifest_json}" network_id)
    if(NOT custom_schema_error STREQUAL "NOTFOUND"
       OR NOT custom_generator_error STREQUAL "NOTFOUND"
       OR NOT custom_id_error STREQUAL "NOTFOUND"
       OR NOT custom_manifest_schema EQUAL 1
       OR custom_generator_version LESS 3
       OR NOT custom_manifest_id STREQUAL ARCH_CUSTOM_NETWORK_ID)
        message(FATAL_ERROR
            "Unsupported or inconsistent custom network manifest: ${custom_manifest}")
    endif()
    list(FIND ARCH_CUSTOM_RESERVED_IDS "${ARCH_CUSTOM_NETWORK_ID}"
        custom_reserved_index)
    string(LENGTH "${ARCH_CUSTOM_NETWORK_ID}" custom_id_length)
    if(NOT ARCH_CUSTOM_NETWORK_ID MATCHES "^[a-z]"
       OR ARCH_CUSTOM_NETWORK_ID MATCHES "[^a-z0-9_]"
       OR custom_id_length GREATER 48
       OR ARCH_CUSTOM_NETWORK_ID MATCHES "^(aprox|iso)"
       OR NOT custom_reserved_index EQUAL -1
       OR NOT custom_folder STREQUAL ARCH_CUSTOM_NETWORK_ID)
        message(FATAL_ERROR
            "Unsafe custom network ID/folder contract in ${custom_config}")
    endif()
    list(FIND ARCH_CUSTOM_DISCOVERED_IDS
        "${ARCH_CUSTOM_NETWORK_ID}" duplicate_custom_index)
    if(NOT duplicate_custom_index EQUAL -1)
        message(FATAL_ERROR
            "Duplicate custom network ID: ${ARCH_CUSTOM_NETWORK_ID}")
    endif()
    list(APPEND ARCH_CUSTOM_DISCOVERED_IDS "${ARCH_CUSTOM_NETWORK_ID}")
    math(EXPR custom_numeric_id "1024 + ${ARCH_CUSTOM_DISCOVERY_COUNT}")
    math(EXPR ARCH_CUSTOM_DISCOVERY_COUNT
        "${ARCH_CUSTOM_DISCOVERY_COUNT} + 1")

    file(REAL_PATH "${custom_dir}" custom_dir_real)
    file(REAL_PATH "${ARCH_CUSTOM_NETWORK_HEADER}" custom_header_real)
    file(REAL_PATH "${ARCH_CUSTOM_NETWORK_SOURCE}" custom_source_real)
    cmake_path(IS_PREFIX custom_dir_real "${custom_header_real}"
        NORMALIZE custom_header_inside)
    cmake_path(IS_PREFIX custom_dir_real "${custom_source_real}"
        NORMALIZE custom_source_inside)
    if(NOT custom_header_inside OR NOT custom_source_inside)
        message(FATAL_ERROR
            "Custom network source escaped its package: ${custom_config}")
    endif()

    if(ARCH_CUSTOM_NETWORKS)
        list(FIND ARCH_CUSTOM_NETWORKS "${ARCH_CUSTOM_NETWORK_ID}" custom_index)
        if(custom_index EQUAL -1)
            continue()
        endif()
    endif()
    string(JSON custom_species ERROR_VARIABLE custom_species_error
        GET "${custom_manifest_json}" species_count)
    string(JSON custom_auxiliary ERROR_VARIABLE custom_auxiliary_error
        GET "${custom_manifest_json}" auxiliary_equations)
    if(NOT custom_auxiliary_error STREQUAL "NOTFOUND")
        set(custom_auxiliary 0) # Packages predating integrated source states.
    endif()
    if(NOT custom_species_error STREQUAL "NOTFOUND"
       OR NOT custom_species MATCHES "^[1-9][0-9]*$"
       OR NOT custom_auxiliary MATCHES "^[01]$")
        message(FATAL_ERROR "Invalid custom network ODE layout: ${custom_manifest}")
    endif()
    string(APPEND ARCH_CUSTOM_TYPES_CONTENT
        "#include \"${ARCH_CUSTOM_NETWORK_HEADER}\"\n"
        "static_assert(${ARCH_CUSTOM_NETWORK_TYPE}::NUM_SPECIES == ${custom_species}, \"Network species metadata mismatch\");\n"
        "static_assert(${ARCH_CUSTOM_NETWORK_TYPE}::ODE_NEQ == ${custom_species} + 1 + ${custom_auxiliary}, \"Network ODE metadata mismatch\");\n")
    set(custom_token "Custom_${ARCH_CUSTOM_NETWORK_ID}")
    string(APPEND ARCH_CUSTOM_LAYOUT " \\")
    string(APPEND ARCH_CUSTOM_LAYOUT
        "\n    M(${custom_token}, ${custom_species}, ${custom_auxiliary})")
    set(custom_entry
        "M(${custom_token}, ${custom_numeric_id}, \"custom:${ARCH_CUSTOM_NETWORK_ID}\", ${ARCH_CUSTOM_NETWORK_TYPE})")
    string(APPEND ARCH_CUSTOM_ALL " \\")
    string(APPEND ARCH_CUSTOM_ALL "\n    ${custom_entry}")
    # Device registration requires a generated host/device mathematical body,
    # immutable device-accessible tables and a lowered Jacobian structure.
    # Packages without this contract retain a CPU-only execution route.
    string(JSON custom_device_math ERROR_VARIABLE custom_device_error
        GET "${custom_manifest_json}" device_callable_math)
    string(JSON custom_device_type ERROR_VARIABLE custom_device_type_error
        TYPE "${custom_manifest_json}" device_callable_math)
    if(custom_generator_version GREATER_EQUAL 4
       AND custom_device_error STREQUAL "NOTFOUND"
       AND custom_device_type STREQUAL "BOOLEAN" AND custom_device_math)
        string(APPEND ARCH_CUSTOM_CUDA " \\")
        string(APPEND ARCH_CUSTOM_CUDA "\n    ${custom_entry}")
        list(APPEND ARCH_CUSTOM_CUDA_IDS "${ARCH_CUSTOM_NETWORK_ID}")
        set(ARCH_CUSTOM_TYPE_${ARCH_CUSTOM_NETWORK_ID} "${ARCH_CUSTOM_NETWORK_TYPE}")
        set(ARCH_CUSTOM_HEADER_${ARCH_CUSTOM_NETWORK_ID} "${ARCH_CUSTOM_NETWORK_HEADER}")
        set(ARCH_CUSTOM_AUXILIARY_${ARCH_CUSTOM_NETWORK_ID} "${custom_auxiliary}")
        math(EXPR ARCH_CUSTOM_EQUATIONS_${ARCH_CUSTOM_NETWORK_ID}
            "${custom_species} + 1 + ${custom_auxiliary}")
        math(EXPR ARCH_CUSTOM_CUDA_COUNT "${ARCH_CUSTOM_CUDA_COUNT} + 1")
    else()
        string(APPEND ARCH_CUSTOM_CPU_ONLY " \\")
        string(APPEND ARCH_CUSTOM_CPU_ONLY "\n    ${custom_entry}")
        math(EXPR ARCH_CUSTOM_CPU_ONLY_COUNT "${ARCH_CUSTOM_CPU_ONLY_COUNT} + 1")
    endif()
    list(APPEND ARCH_APPLICATION_SOURCES "${ARCH_CUSTOM_NETWORK_SOURCE}")
    math(EXPR ARCH_CUSTOM_COUNT "${ARCH_CUSTOM_COUNT} + 1")
    message(STATUS "[NET] Registered custom:${ARCH_CUSTOM_NETWORK_ID}")
endforeach()
foreach(requested_custom_id IN LISTS ARCH_CUSTOM_NETWORKS)
    list(FIND ARCH_CUSTOM_DISCOVERED_IDS
        "${requested_custom_id}" requested_custom_index)
    if(requested_custom_index EQUAL -1)
        message(FATAL_ERROR
            "ARCH_CUSTOM_NETWORKS requested an unknown ID: ${requested_custom_id}")
    endif()
endforeach()
string(APPEND ARCH_CUSTOM_REGISTRY_CONTENT
    "#define ARCH_CUSTOM_NETWORK_COUNT ${ARCH_CUSTOM_COUNT}\n"
    "#define ARCH_CUSTOM_CPU_ONLY_NETWORK_COUNT ${ARCH_CUSTOM_CPU_ONLY_COUNT}\n"
    "#define ARCH_CUSTOM_CUDA_NETWORK_COUNT ${ARCH_CUSTOM_CUDA_COUNT}\n"
    "${ARCH_CUSTOM_ALL}\n${ARCH_CUSTOM_CPU_ONLY}\n${ARCH_CUSTOM_CUDA}\n${ARCH_CUSTOM_LAYOUT}\n")
file(CONFIGURE OUTPUT "${ARCH_CUSTOM_REGISTRY_HEADER}"
    CONTENT "${ARCH_CUSTOM_REGISTRY_CONTENT}" @ONLY NEWLINE_STYLE UNIX)
file(CONFIGURE OUTPUT "${ARCH_CUSTOM_TYPES_HEADER}"
    CONTENT "${ARCH_CUSTOM_TYPES_CONTENT}" @ONLY NEWLINE_STYLE UNIX)
list(REMOVE_DUPLICATES ARCH_APPLICATION_SOURCES)
