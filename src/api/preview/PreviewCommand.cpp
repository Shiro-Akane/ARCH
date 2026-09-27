/**
 * @file PreviewCommand.cpp
 * @brief Parse one preview CLI request and emit one protocol response.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Parse one preview CLI request and emit one protocol response.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#include <charconv>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>

#include "api/CaseInspection.h"
#include "api/Configuration.h"
#include "api/Preview.h"
#include "api/PreviewSession.h"
#include "api/preview/ResourceEstimates.h"
#include "api/protocol/RequestInput.h"
#include "api/resources/WorkerLimits.h"

namespace arch::api {

/** Parse a one-shot inspection/preview CLI command and emit its response. */
int RunPreviewCommand(int argc, char **argv) {
    PreviewResponse response;
    using contract::Command;
    const auto* definition = argc > 1 ? contract::find(argv[1]) : nullptr;
    const auto command = definition ? definition->command : Command::Fields;
    const bool mesh = command == Command::Mesh;
    try {
        if (!definition) throw std::invalid_argument("Unknown API command");
        if (command == Command::Session) {
            if (argc != 2) throw std::invalid_argument("--preview-session takes no arguments");
            return RunPreviewSession();
        }
        if (!definition->takes_configuration) {
            if (argc != 2) throw std::invalid_argument(std::string(definition->flag) + " takes no arguments");
            if (command == Command::Cases) std::cout << RegisteredCases() << '\n';
            else if (command == Command::Schema) std::cout << ConfigurationSchema().dump(max_response_bytes - 1) << '\n';
            else std::cout << PreviewCapabilities() << '\n';
            return 0;
        }
        if (argc < 4)
            throw std::invalid_argument("Usage: ARCH " + std::string(definition->flag) + " CASE --config-stdin [--request-id ID]");
        PreviewRequest request;
        request.case_id = argv[2];
        request.initial_mesh = mesh;
        std::set<std::string> seen;
        bool config_stdin = false;
        for (int i = 3; i < argc; ++i) {
            const std::string option = argv[i];
            if (!seen.insert(option).second) throw std::invalid_argument("Duplicate preview option");
            if (option == "--config-stdin") config_stdin = true;
            else if (option == "--samples" || option == "--samples-x1" || option == "--samples-x2" || option == "--request-id" || option == "--mesh-max-blocks" || option == "--mesh-memory-mib") {
                if (++i == argc) throw std::invalid_argument("Missing preview option value");
                const std::string_view value = argv[i];
                if (option == "--request-id") request.request_id = value;
                else {
                    if (command != Command::Fields && !mesh) throw std::invalid_argument("Configuration inspection does not accept sampling options");
                    int count = 0;
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                        throw std::invalid_argument(option + " expects a whole number representable as int");
                    if (option.starts_with("--mesh-")) {
                        if (!mesh) throw std::invalid_argument("Mesh budgets require --preview-amr");
                        if (option == "--mesh-max-blocks") {
                            if (count < 1 || count > contract::mesh_max_blocks) throw std::invalid_argument("Mesh block budget must be 1..1024");
                            request.mesh_max_blocks = count;
                        } else {
                            if (count < contract::mesh_min_memory_mib || count > contract::mesh_max_memory_mib) throw std::invalid_argument("Mesh memory budget must be 16..256 MiB");
                            request.mesh_memory_mib = count;
                        }
                    } else if (mesh) throw std::invalid_argument("AMR mesh has no sampling shape options");
                    else if (option == "--samples-x1") request.samples_x1 = count;
                    else if (option == "--samples-x2") request.samples_x2 = count;
                    else { request.sample_count = count; request.sample_count_provided = true; }
                }
            } else throw std::invalid_argument("Unknown preview option");
        }
        if (!config_stdin) throw std::invalid_argument("--config-stdin is required");
        if (!detail::ValidUtf8(request.case_id) || !detail::ValidUtf8(request.request_id))
            throw std::invalid_argument("Identifiers must be UTF-8 without NUL bytes");
        if (request.case_id.size() > 128 || request.request_id.size() > 128)
            throw std::invalid_argument("Expected identifiers <= 128 bytes");
        char buffer[8192];
        while (std::cin) {
            std::cin.read(buffer, sizeof(buffer));
            const auto count = std::cin.gcount();
            if (request.config_text.size() + count > max_config_bytes)
                throw std::invalid_argument("Configuration exceeds 1 MiB");
            request.config_text.append(buffer, static_cast<std::size_t>(count));
        }
        if (std::cin.bad()) throw std::runtime_error("Failed to read configuration from stdin");
        if (!detail::ValidUtf8(request.config_text)) throw std::invalid_argument("Configuration must be UTF-8 without NUL bytes");
        if (mesh || command == Command::InspectCase)
            ApplyInspectionProcessLimits(command == Command::InspectCase ? contract::case_cpu_seconds : contract::worker_cpu_seconds);
        switch (command) {
            case Command::Resources: response = EstimateAmrResources(request); break;
            case Command::Configuration: response = InspectConfiguration(request); break;
            case Command::InspectCase: response = InspectCase(request); break;
            default: response = GeneratePreview(request); break;
        }
    } catch (const std::exception &error) {
        if (!definition || command == Command::Fields) response = PreviewInputError(error.what());
        else response = {detail::Json::object({{"schemaVersion", contract::schema_version}, {"version", std::string(definition->version)},
            {"kind", std::string(definition->kind)}, {"status", "error"}, {"identity", detail::Json()},
            {"diagnostics", detail::Json::array({detail::Json::object({
                {"severity", "error"}, {"code", "INVALID_REQUEST"}, {"message", error.what()}})})}}).dump(), 2};
    }
    std::cout << response.json << '\n';
    return response.exit_code;
}
} // namespace arch::api
