#pragma once
#include <array>
#include <string_view>
namespace arch::api::contract {
inline constexpr auto schema_version = "1.0";
inline constexpr auto configuration_version = "2";
inline constexpr auto initialization_version = "1";
enum class Command { Fields, Capabilities, Schema, Configuration, Cases, Resources, Mesh, InspectCase };
struct CommandDefinition { Command command; std::string_view flag, kind, version; bool takes_configuration; };
inline constexpr std::array commands{
    CommandDefinition{Command::Fields, "--preview", "initial-state-preview", "1", true},
    CommandDefinition{Command::Capabilities, "--preview-capabilities", "preview-capabilities", "1", false},
    CommandDefinition{Command::Schema, "--config-schema", "configuration-schema", configuration_version, false},
    CommandDefinition{Command::Configuration, "--inspect-config", "configuration-inspection", configuration_version, true},
    CommandDefinition{Command::Cases, "--list-cases", "registered-cases", "1", false},
    CommandDefinition{Command::Resources, "--amr-resources", "amr-resource-estimate", "1", true},
    CommandDefinition{Command::Mesh, "--preview-amr", "initial-amr-preview", "1", true},
    CommandDefinition{Command::InspectCase, "--inspect-case", "case-inspection", initialization_version, true}
};
inline const CommandDefinition* find(std::string_view flag) {
    for (const auto& definition : commands) if (definition.flag == flag) return &definition;
    return nullptr;
}
inline constexpr int mesh_default_blocks = 512, mesh_max_blocks = 1024;
inline constexpr int mesh_default_memory_mib = 128, mesh_min_memory_mib = 16, mesh_max_memory_mib = 256;
inline constexpr int worker_address_space_mib = 1024, worker_cpu_seconds = 30, mesh_seconds = 10;
inline constexpr int case_cpu_seconds = 300, case_wall_seconds = 360;
} // namespace arch::api::contract
