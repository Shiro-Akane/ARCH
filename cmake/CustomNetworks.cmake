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
set(ARCH_CUSTOM_NSE "#define ARCH_FOR_EACH_CUSTOM_NETWORK_NSE(M)")
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
    # Older packages retain ordinary burning without claiming unexported NSE
    # data. New packages certify a physical subset independently of backend.
    set(custom_nse false)
    set(custom_nse_reason "missing_nse_metadata")
    if(custom_generator_version GREATER_EQUAL 5)
        foreach(field schema_version eligible reason species_count constraint_rank
                      strong_stoichiometric_rank partition_policy screening_policy weak_policy
                      mass_convention energy_reference)
            string(JSON custom_nse_${field} ERROR_VARIABLE custom_nse_error
                GET "${custom_manifest_json}" nse "${field}")
            if(NOT custom_nse_error STREQUAL "NOTFOUND")
                message(FATAL_ERROR "Missing NSE ${field}: ${custom_manifest}")
            endif()
        endforeach()
        foreach(field schema_version species_count constraint_rank strong_stoichiometric_rank)
            string(JSON custom_nse_numeric_type TYPE "${custom_manifest_json}" nse "${field}")
            if(NOT custom_nse_numeric_type STREQUAL "NUMBER"
               OR NOT custom_nse_${field} MATCHES "^[0-9]+$")
                message(FATAL_ERROR "NSE ${field} must be an integer: ${custom_manifest}")
            endif()
        endforeach()
        string(JSON custom_nse_type TYPE "${custom_manifest_json}" nse eligible)
        string(JSON custom_supports_nse_type TYPE "${custom_manifest_json}" supports_nse)
        string(JSON custom_supports_nse GET "${custom_manifest_json}" supports_nse)
        string(JSON custom_nse_extent LENGTH "${custom_manifest_json}" nse species)
        string(JSON custom_nse_reason_count LENGTH "${custom_manifest_json}" nse reasons)
        string(JSON custom_nse_missing_count LENGTH "${custom_manifest_json}" nse missing_species)
        if(NOT custom_nse_schema_version EQUAL 1
           OR NOT custom_nse_type STREQUAL "BOOLEAN"
           OR NOT custom_supports_nse_type STREQUAL "BOOLEAN"
           OR NOT custom_supports_nse STREQUAL custom_nse_eligible
           OR NOT custom_nse_species_count EQUAL custom_species
           OR NOT custom_nse_extent EQUAL custom_species
           OR NOT custom_nse_reason MATCHES "^[a-z_]+(,[a-z_]+)*$"
           OR NOT custom_nse_constraint_rank MATCHES "^[12]$"
           OR NOT custom_nse_strong_stoichiometric_rank MATCHES "^[0-9]+$")
            message(FATAL_ERROR "Inconsistent NSE metadata: ${custom_manifest}")
        endif()
        if(custom_nse_eligible)
            math(EXPR custom_nse_total_rank
                "${custom_nse_constraint_rank} + ${custom_nse_strong_stoichiometric_rank}")
            if(NOT custom_nse_total_rank EQUAL custom_species
               OR NOT custom_auxiliary EQUAL 0
               OR NOT custom_nse_reason_count EQUAL 0
               OR NOT custom_nse_missing_count EQUAL 0
               OR NOT custom_nse_partition_policy STREQUAL "ground_state_only"
               OR NOT custom_nse_screening_policy STREQUAL "none"
               OR NOT custom_nse_weak_policy STREQUAL "none"
               OR NOT custom_nse_mass_convention STREQUAL "pynucastro_Nucleus_A_nuc_and_nucbind_same_package"
               OR NOT custom_nse_energy_reference STREQUAL "generated_mion_conserved_baryon_gauge"
               OR NOT custom_nse_reason STREQUAL "eligible_ground_state_detailed_balance")
                message(FATAL_ERROR "Unsupported NSE physical contract: ${custom_manifest}")
            endif()
            foreach(constant NSE_AVOGADRO NSE_K_BOLTZMANN NSE_K_BOLTZMANN_MEV
                             NSE_PLANCK NSE_HBAR NSE_ATOMIC_MASS_UNIT NSE_MEV_TO_ERG)
                string(JSON constant_type TYPE "${custom_manifest_json}" nse constants "${constant}")
                string(JSON constant_value GET "${custom_manifest_json}" nse constants "${constant}")
                if(NOT constant_type STREQUAL "NUMBER" OR NOT constant_value GREATER 0)
                    message(FATAL_ERROR "Invalid NSE constant ${constant}: ${custom_manifest}")
                endif()
            endforeach()
            math(EXPR custom_nse_last "${custom_species} - 1")
            foreach(species_index RANGE 0 ${custom_nse_last})
                string(JSON metadata_name GET "${custom_manifest_json}" nse species ${species_index} name)
                string(JSON registry_name GET "${custom_manifest_json}" species ${species_index})
                if(NOT metadata_name STREQUAL registry_name)
                    message(FATAL_ERROR "NSE species order differs: ${custom_manifest}")
                endif()
                string(JSON reliable_type TYPE "${custom_manifest_json}" nse species ${species_index} spin_reliable)
                string(JSON reliable_value GET "${custom_manifest_json}" nse species ${species_index} spin_reliable)
                if(NOT reliable_type STREQUAL "BOOLEAN" OR NOT reliable_value)
                    message(FATAL_ERROR "NSE requires reliable ground-state spins: ${custom_manifest}")
                endif()
                foreach(property A Z mass_mev mass_amu binding_mev spin_weight)
                    string(JSON property_type TYPE "${custom_manifest_json}" nse species ${species_index} "${property}")
                    string(JSON property_value GET "${custom_manifest_json}" nse species ${species_index} "${property}")
                    if(NOT property_type STREQUAL "NUMBER")
                        message(FATAL_ERROR "Invalid NSE nuclear data ${property}: ${custom_manifest}")
                    endif()
                    if((property STREQUAL "A" OR property STREQUAL "Z")
                       AND NOT property_value MATCHES "^[0-9]+$")
                        message(FATAL_ERROR "NSE ${property} must be a nonnegative integer: ${custom_manifest}")
                    endif()
                    if(NOT property STREQUAL "Z" AND NOT property STREQUAL "binding_mev"
                       AND NOT property_value GREATER 0)
                        message(FATAL_ERROR "Nonpositive NSE nuclear data ${property}: ${custom_manifest}")
                    endif()
                endforeach()
                string(JSON nuclear_a GET "${custom_manifest_json}" nse species ${species_index} A)
                string(JSON nuclear_z GET "${custom_manifest_json}" nse species ${species_index} Z)
                if(nuclear_z GREATER nuclear_a)
                    message(FATAL_ERROR "NSE charge exceeds baryon number: ${custom_manifest}")
                endif()
            endforeach()
            set(custom_nse true)
        elseif(custom_nse_reason STREQUAL "eligible_ground_state_detailed_balance")
            message(FATAL_ERROR "Ineligible NSE package has no rejection reason: ${custom_manifest}")
        endif()
    endif()
    string(APPEND ARCH_CUSTOM_TYPES_CONTENT
        "#include \"${ARCH_CUSTOM_NETWORK_HEADER}\"\n"
        "static_assert(${ARCH_CUSTOM_NETWORK_TYPE}::NUM_SPECIES == ${custom_species}, \"Network species metadata mismatch\");\n"
        "static_assert(${ARCH_CUSTOM_NETWORK_TYPE}::ODE_NEQ == ${custom_species} + 1 + ${custom_auxiliary}, \"Network ODE metadata mismatch\");\n")
    if(custom_generator_version GREATER_EQUAL 5)
        string(APPEND ARCH_CUSTOM_TYPES_CONTENT
            "static_assert(${ARCH_CUSTOM_NETWORK_TYPE}::NSE_DATA_VERSION == 1, \"Network NSE data version mismatch\");\n"
            "static_assert(${ARCH_CUSTOM_NETWORK_TYPE}::NSE_CONSTRAINT_RANK == ${custom_nse_constraint_rank}, \"Network NSE constraint rank mismatch\");\n"
            "static_assert(${ARCH_CUSTOM_NETWORK_TYPE}::SUPPORTS_NSE == ${custom_nse}, \"Network NSE eligibility mismatch\");\n")
    endif()
    set(custom_token "Custom_${ARCH_CUSTOM_NETWORK_ID}")
    string(APPEND ARCH_CUSTOM_NSE " \\")
    string(APPEND ARCH_CUSTOM_NSE
        "\n    M(${custom_token}, ${custom_nse}, \"${custom_nse_reason}\")")
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
    "${ARCH_CUSTOM_ALL}\n${ARCH_CUSTOM_CPU_ONLY}\n${ARCH_CUSTOM_CUDA}\n${ARCH_CUSTOM_LAYOUT}\n${ARCH_CUSTOM_NSE}\n")
file(CONFIGURE OUTPUT "${ARCH_CUSTOM_REGISTRY_HEADER}"
    CONTENT "${ARCH_CUSTOM_REGISTRY_CONTENT}" @ONLY NEWLINE_STYLE UNIX)
file(CONFIGURE OUTPUT "${ARCH_CUSTOM_TYPES_HEADER}"
    CONTENT "${ARCH_CUSTOM_TYPES_CONTENT}" @ONLY NEWLINE_STYLE UNIX)
list(REMOVE_DUPLICATES ARCH_APPLICATION_SOURCES)
