/** Check OS run identities and the actual running-image fingerprint. */
#include "core/files/FileFingerprint.h"
#include "core/files/RunIdentity.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

int main(int argc, char** argv)
{
    try {
        // A parent can rename/replace the launch path before the first digest.
        if (argc == 2 && std::string(argv[1]) == "--fingerprint-before") {
            std::cout << "READY" << std::endl;
            std::string resume;
            std::getline(std::cin, resume);
            const auto& digest = arch::core::running_executable_sha256();
            std::cout << "DIGEST " << digest << std::endl;
            if (arch::core::running_executable_sha256() != digest)
                throw std::runtime_error("Cached running-image digest changed");
            return 0;
        }
        if (argc != 1) throw std::runtime_error("Unexpected test argument");
        std::unordered_set<std::string> identities;
        for (int i = 0; i < 16; ++i) {
            const auto id = arch::core::new_run_identity();
            if (!arch::core::valid_run_identity(id) || !identities.insert(id).second)
                throw std::runtime_error("Invalid or repeated run identity");
        }
        const auto& digest = arch::core::running_executable_sha256();
        if (digest.size() != 64 || digest.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("Missing running-image SHA-256");
        if (arch::core::string_sha256("abc") !=
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
            throw std::runtime_error("SHA-256 reference mismatch");
        if (std::string(arch::core::running_executable_source()) == "unknown")
            throw std::runtime_error("Missing running-image source");
        std::cout << "PLATFORM_FILES_PASS source=" << arch::core::running_executable_source()
                  << " sha256=" << digest << std::endl;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
