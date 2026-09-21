#pragma once

#include "ApplicationContract.h"
#include <cstddef>
#include <optional>
#include <string>

namespace arch::api {
inline constexpr const char *preview_schema_version = contract::schema_version;
inline constexpr std::size_t max_config_bytes = 1024 * 1024;
inline constexpr int default_sample_count = 512;
inline constexpr int max_sample_count = 4096;
inline constexpr std::size_t max_response_bytes = 8 * 1024 * 1024;
inline constexpr int default_samples_2d = 128;
inline constexpr int max_samples_per_axis_2d = 256;
inline constexpr std::size_t max_total_samples_2d = 65536;
inline constexpr std::size_t max_preview_fields = 7;

struct PreviewRequest {
    std::string case_id;
    std::string config_text;
    std::string request_id;
    bool initial_mesh = false;
    int mesh_max_blocks = contract::mesh_default_blocks;
    int mesh_memory_mib = contract::mesh_default_memory_mib;
    int sample_count = default_sample_count;
    bool sample_count_provided = false;
    std::optional<int> samples_x1, samples_x2;
};
struct PreviewResponse {
    std::string json;
    int exit_code = 0;
};

// Internal application boundary; the public compatibility contract is the CLI
// and JSON in README.md. One request per process (existing EOS caches/logging
// are process-owned). Optional bounded CPU initial hierarchy; no time stepping,
// device probe or scientific output writer.
PreviewResponse GeneratePreview(const PreviewRequest &request);
std::string PreviewCapabilities();
PreviewResponse PreviewInputError(const std::string &message);
int RunPreviewCommand(int argc, char **argv);
} // namespace arch::api
