#pragma once

#include <cstddef>
#include <string>

namespace arch::api {
inline constexpr const char *preview_schema_version = "1.0";
inline constexpr std::size_t max_config_bytes = 1024 * 1024;
inline constexpr int default_sample_count = 512;
inline constexpr int max_sample_count = 4096;
inline constexpr std::size_t max_response_bytes = 8 * 1024 * 1024;

struct PreviewRequest {
    std::string case_id;
    std::string config_text;
    std::string request_id;
    int sample_count = default_sample_count;
};
struct PreviewResponse {
    std::string json;
    int exit_code = 0;
};

// Internal application boundary; the public compatibility contract is the CLI
// and JSON in README.md. One request per process (existing EOS caches/logging
// are process-owned). No Driver, AMR hierarchy, device probe or output writer.
PreviewResponse GeneratePreview(const PreviewRequest &request);
std::string PreviewCapabilities();
PreviewResponse PreviewInputError(const std::string &message);
int RunPreviewCommand(int argc, char **argv);
} // namespace arch::api
