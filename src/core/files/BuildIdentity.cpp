/**
 * @file BuildIdentity.cpp
 * @brief Bind runtime output to the configured project sources and compiler profile.
 *
 * Workflow:
 * 1. Consume only generated build-time records, never the runtime checkout HEAD.
 * 2. Hash source and profile records, then bind both into one scoped build ID.
 * 3. Cache the immutable result for all output sessions in this executable.
 *
 * build_id = SHA256(version || source_manifest_sha256 || profile_sha256).
 * The running ELF digest remains separate to avoid a self-referential hash.
 */

#include <stdexcept>
#include <string>

#include "core/files/BuildIdentity.h"
#include "core/files/FileFingerprint.h"

#if __has_include("BuildIdentity.generated.h")
#include "BuildIdentity.generated.h"
#endif
#ifndef ARCH_IDENTITY_BUILD_CONFIG
#define ARCH_IDENTITY_BUILD_CONFIG "unspecified"
#endif

namespace arch::core {
/** Construct the immutable scoped identity from actual generated records. */
const BuildIdentity& compiled_build_identity()
{
    static const BuildIdentity identity = [] {
#if __has_include("BuildIdentity.generated.h")
        BuildIdentity result;
        result.version = "arch-build-identity-1";
        result.scope = "project-source-and-configured-compiler-profile";
        result.source_manifest = arch_generated_identity::source_manifest;
        result.profile_record = std::string(arch_generated_identity::profile_record)
            + "\nselected-configuration=" ARCH_IDENTITY_BUILD_CONFIG "\n";
        if (result.source_manifest.empty() || result.profile_record.empty()
            || std::string(ARCH_IDENTITY_BUILD_CONFIG) == "unspecified")
            throw std::runtime_error("Compiled build identity is incomplete");
        result.source_manifest_sha256 = string_sha256(result.source_manifest);
        result.profile_sha256 = string_sha256(result.profile_record);
        result.build_id = string_sha256(result.version + "\n" + result.source_manifest_sha256
                                        + "\n" + result.profile_sha256 + "\n");
        result.source_git_head = arch_generated_identity::source_git_head;
        result.source_git_dirty = arch_generated_identity::source_git_dirty;
        return result;
#else
        throw std::runtime_error("This executable has no generated build provenance");
        return BuildIdentity{};
#endif
    }();
    return identity;
}
} // namespace arch::core
