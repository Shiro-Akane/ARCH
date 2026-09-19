#include "Preview.h"

#include <charconv>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>

namespace arch::api {
namespace {
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first == 0) return false;
        if (first < 0x80) continue;
        int more = 0;
        unsigned value = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { more = 1; value = first & 31; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { more = 2; value = first & 15; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { more = 3; value = first & 7; minimum = 0x10000; }
        else return false;
        while (more--) {
            if (i == text.size()) return false;
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
} // namespace

int RunPreviewCommand(int argc, char **argv) {
    PreviewResponse response;
    try {
        if (std::string_view(argv[1]) == "--preview-capabilities") {
            if (argc != 2) throw std::invalid_argument("--preview-capabilities takes no arguments");
            std::cout << PreviewCapabilities() << '\n';
            return 0;
        }
        if (argc < 4)
            throw std::invalid_argument("Usage: ARCH --preview Sod --config-stdin [--samples 512] [--request-id ID]");
        PreviewRequest request;
        request.case_id = argv[2];
        std::set<std::string> seen;
        bool config_stdin = false;
        for (int i = 3; i < argc; ++i) {
            const std::string option = argv[i];
            if (!seen.insert(option).second) throw std::invalid_argument("Duplicate preview option");
            if (option == "--config-stdin") config_stdin = true;
            else if (option == "--samples" || option == "--request-id") {
                if (++i == argc) throw std::invalid_argument("Missing preview option value");
                const std::string_view value = argv[i];
                if (option == "--request-id") request.request_id = value;
                else {
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), request.sample_count);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                        throw std::invalid_argument("--samples expects a whole number");
                }
            } else throw std::invalid_argument("Unknown preview option");
        }
        if (!config_stdin) throw std::invalid_argument("--config-stdin is required");
        if (!valid_utf8(request.case_id) || !valid_utf8(request.request_id))
            throw std::invalid_argument("Identifiers must be UTF-8 without NUL bytes");
        if (request.sample_count < 2 || request.sample_count > max_sample_count
            || request.case_id.size() > 128 || request.request_id.size() > 128)
            throw std::invalid_argument("Expected 2..4096 samples and identifiers <= 128 bytes");
        char buffer[8192];
        while (std::cin) {
            std::cin.read(buffer, sizeof(buffer));
            const auto count = std::cin.gcount();
            if (request.config_text.size() + count > max_config_bytes)
                throw std::invalid_argument("Configuration exceeds 1 MiB");
            request.config_text.append(buffer, static_cast<std::size_t>(count));
        }
        if (std::cin.bad()) throw std::runtime_error("Failed to read configuration from stdin");
        if (!valid_utf8(request.config_text)) throw std::invalid_argument("Configuration must be UTF-8 without NUL bytes");
        response = GeneratePreview(request);
    } catch (const std::exception &error) {
        response = PreviewInputError(error.what());
    }
    std::cout << response.json << '\n';
    return response.exit_code;
}
} // namespace arch::api
