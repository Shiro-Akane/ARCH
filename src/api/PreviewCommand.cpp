#include "Preview.h"
#include "Configuration.h"

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
    const bool inspect = std::string_view(argv[1]) == "--inspect-config";
    try {
        if (std::string_view(argv[1]) == "--config-schema") {
            if (argc != 2) throw std::invalid_argument("--config-schema takes no arguments");
            std::cout << ConfigurationSchema().dump(max_response_bytes) << '\n';
            return 0;
        }
        if (std::string_view(argv[1]) == "--preview-capabilities") {
            if (argc != 2) throw std::invalid_argument("--preview-capabilities takes no arguments");
            std::cout << PreviewCapabilities() << '\n';
            return 0;
        }
        if (argc < 4)
            throw std::invalid_argument(inspect
                ? "Usage: ARCH --inspect-config CASE --config-stdin [--request-id ID]"
                : "Usage: ARCH --preview CASE --config-stdin [--samples N | --samples-x1 NX --samples-x2 NY] [--request-id ID]");
        PreviewRequest request;
        request.case_id = argv[2];
        std::set<std::string> seen;
        bool config_stdin = false;
        for (int i = 3; i < argc; ++i) {
            const std::string option = argv[i];
            if (!seen.insert(option).second) throw std::invalid_argument("Duplicate preview option");
            if (option == "--config-stdin") config_stdin = true;
            else if (option == "--samples" || option == "--samples-x1" || option == "--samples-x2" || option == "--request-id") {
                if (++i == argc) throw std::invalid_argument("Missing preview option value");
                const std::string_view value = argv[i];
                if (option == "--request-id") request.request_id = value;
                else {
                    if (inspect) throw std::invalid_argument("Configuration inspection does not accept sampling options");
                    int count = 0;
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                        throw std::invalid_argument(option + " expects a whole number representable as int");
                    if (option == "--samples-x1") request.samples_x1 = count;
                    else if (option == "--samples-x2") request.samples_x2 = count;
                    else { request.sample_count = count; request.sample_count_provided = true; }
                }
            } else throw std::invalid_argument("Unknown preview option");
        }
        if (!config_stdin) throw std::invalid_argument("--config-stdin is required");
        if (!valid_utf8(request.case_id) || !valid_utf8(request.request_id))
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
        if (!valid_utf8(request.config_text)) throw std::invalid_argument("Configuration must be UTF-8 without NUL bytes");
        response = inspect ? InspectConfiguration(request) : GeneratePreview(request);
    } catch (const std::exception &error) {
        if (inspect || std::string_view(argv[1]) == "--config-schema") {
            response = {detail::Json::object({{"schemaVersion", "1.0"}, {"version", "1"},
                {"kind", inspect ? "configuration-inspection" : "configuration-schema"},
                {"status", "error"}, {"identity", detail::Json()},
                {"diagnostics", detail::Json::array({detail::Json::object({
                    {"severity", "error"}, {"code", "INVALID_REQUEST"}, {"message", error.what()}})})}}).dump(), 2};
        } else response = PreviewInputError(error.what());
    }
    std::cout << response.json << '\n';
    return response.exit_code;
}
} // namespace arch::api
